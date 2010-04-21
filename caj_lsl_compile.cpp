#include "caj_lsl_parse.h"
#include "caj_vm.h"
#include "caj_vm_asm.h"
#include "caj_vm_ops.h"

#include <map>
#include <string>
#include <vector>
#include <cassert>
#include <stdio.h>
#include <stdarg.h>
#include <fcntl.h>

// Possible Linden dain bramage:
// Order of operations (second operand, first operand)
// String/key typecasts: https://jira.secondlife.com/browse/SVC-1710
// ...

struct var_desc {
  uint8_t type; uint8_t is_global; uint16_t offset;
};

struct var_scope {
  std::map<std::string, var_desc> vars;
  struct var_scope *parent;

  var_scope() : parent(NULL) {
  }
};

struct lsl_compile_state {
  int error; int line_no, column_no;
  std::map<std::string, var_desc> globals;
  // std::map<std::string, var_desc> vars; // locals
  std::map<std::string, const vm_function*> funcs;
  std::map<std::string, function*> sys_funcs;
  std::map<std::string, int> states;
  loc_atom var_stack;
  var_scope *scope;
};

static const vm_function *make_function(vm_asm &vasm, function *func, int state_no = -1);
static uint32_t constify_list(vm_asm &vasm, lsl_compile_state &st, 
			      expr_node *expr);

static void update_loc(lsl_compile_state &st, expr_node *expr) {
  st.line_no = expr->loc.first_line;
  st.column_no = expr->loc.first_column;
}

static void do_error(lsl_compile_state &st, const char* format, ...) {
  va_list args;
  if(st.error != 0) return;
  printf("(%i, %i): ", st.line_no, st.column_no);
  va_start (args, format);
  vprintf (format, args);
  va_end (args);
  st.error = 1;
}

static void handle_arg_vars(vm_asm &vasm, lsl_compile_state &st,
			    func_arg *args, const vm_function* func,
			    var_scope *scope) {
  for(int arg_no = 0; args != NULL; args = args->next, arg_no++) {
    if(scope->vars.count(args->name)) {
      printf("ERROR: duplicate function argument %s\n",args->name);
      st.error = 1; return;
    } else {
      var_desc var; var.type = args->vtype; var.is_global = 0;
      var.offset = func->arg_offsets[arg_no];
      scope->vars[args->name] = var;
      // printf("DEBUG: added argument %s\n", args->name);
    }
  }
}

static int statement_child_count(lsl_compile_state &st, statement *statem) {
    switch(statem->stype) {
    case STMT_DECL: return 0;
    case STMT_EXPR: return 0;
    case STMT_IF:
      return 2; // child[1] may be null, but that should be OK!
    case STMT_RET: return 0;
    case STMT_WHILE: return 1;
    case STMT_DO: return 1;
    case STMT_FOR: return 1;
    case STMT_BLOCK: return 1;
    default:
      printf("ERROR:statement_child_count unhandled statement type %i\n", statem->stype);
	st.error = 1; return 0;
    }
}

static void extract_local_vars(vm_asm &vasm, lsl_compile_state &st,
			       statement *statem, var_scope *scope) {
  for( ; statem != NULL; statem = statem->next) {
    if(statem->stype == STMT_DECL) {
      assert(statem->expr[0] != NULL && 
	     statem->expr[0]->node_type == NODE_IDENT);

      if(statem->expr[0]->u.ident.item != NULL) {
	printf("ERROR: silly programmer, item accesses are for expressions\n");
	st.error = 1; return;
      }

      char* name = statem->expr[0]->u.ident.name; 
      uint8_t vtype = statem->expr[0]->vtype;
      if(scope->vars.count(name)) {
	printf("ERROR: duplicate definition of local var %s\n",name);
	st.error = 1; return;
	// FIXME - handle this
      } else {
	var_desc var; var.type = vtype; var.is_global = 0;
	// FIXME - initialise these where possible
	switch(vtype) {
	case VM_TYPE_INT:
	  var.offset = vasm.const_int(0); 
	  break;
	case VM_TYPE_FLOAT:
	  var.offset = vasm.const_real(0.0f); 
	  break;
	case VM_TYPE_STR:
	  var.offset = vasm.const_str("");
	  break;
	case VM_TYPE_LIST:
	  var.offset = vasm.empty_list();
	  break;
	case VM_TYPE_VECT:
	  var.offset = vasm.const_int(0); 
	  vasm.const_int(0); vasm.const_int(0); 
	  break;
	case VM_TYPE_ROT:
	  var.offset = vasm.const_int(0); 
	  vasm.const_int(0); vasm.const_int(0); vasm.const_int(0);
	  break;
	default:
	  printf("ERROR: unknown type of local var %s\n",name);
	  st.error = 1; return;
	  // FIXME - handle this
	}
	scope->vars[name] = var;
      }
    } else {
      int count = statement_child_count(st, statem);
      if(st.error) return;
      for(int i = 0; i < count; i++) {
	var_scope *child_ctx = new var_scope();
	child_ctx->parent = scope;
	extract_local_vars(vasm, st, statem->child[i], child_ctx);
	statem->child_vars[i] = child_ctx;
      }
    }
  }
}

static uint8_t get_insn_ret_type(uint16_t insn) {
  assert(insn < NUM_INSNS);
  return vm_insns[insn].ret;
}


static var_desc get_variable(lsl_compile_state &st, const char* name) {
  var_scope *scope = st.scope;
  std::map<std::string, var_desc>::iterator iter = scope->vars.find(name);
  //printf("DEBUG: searching for var %s in %p\n", name, scope);
  while(iter == scope->vars.end() && scope->parent != NULL) {
    scope = scope->parent;
    iter = scope->vars.find(name);
    //printf("   ...in %p\n", scope);
  }
  if(iter == scope->vars.end()) {
    //printf("   ...in globals\n");
    std::map<std::string, var_desc>::iterator iter2 = st.globals.find(name);
    if(iter2 == st.globals.end()) {
      var_desc desc; desc.type = VM_TYPE_NONE;
      do_error(st, "ERROR: missing variable %s\n", name);
      return desc;
    } else {
      assert(iter2->second.is_global);
      assert(iter2->second.type <= VM_TYPE_MAX);
      return iter2->second;
    }
  } else {
    assert(!iter->second.is_global);
    assert(iter->second.type <= VM_TYPE_MAX);
    return iter->second;
  }
}

static expr_node* arg_implicit_cast(lsl_compile_state &st, expr_node *expr, uint8_t arg_type) {
  if(expr->vtype == arg_type) return expr;
  if(expr->vtype == VM_TYPE_INT && arg_type == VM_TYPE_FLOAT) {
    return enode_cast(expr, arg_type);
  } else if(expr->vtype == VM_TYPE_STR && arg_type == VM_TYPE_KEY) {
    return enode_cast(expr, arg_type); // FIXME?
  } else {
    do_error(st, "ERROR: bad implicit cast from %s to %s\n",
	     type_names[expr->vtype], type_names[arg_type]);
    return expr;
  }
}

static int dotted_item_to_idx(char *item) {
  assert(!(item[0] == 0 || item[1] != 0 ||
	   ((item[0] < 'x' || item[0] > 'z') && item[0] != 's')));
  if(item[0] == 's') return 3;
  else return  item[0] - 'x';
}

static void propagate_types(vm_asm &vasm, lsl_compile_state &st, expr_node *expr) {
  uint16_t insn; uint8_t ltype, rtype; list_node *lnode;
  update_loc(st, expr);
  switch(expr->node_type) {
  case NODE_CONST: break;
  case NODE_IDENT:
    // get_variable does all the nasty error handling for us!
    expr->vtype = get_variable(st, expr->u.ident.name).type;
    if(expr->u.ident.item != NULL && expr->vtype != VM_TYPE_VECT &&
       expr->vtype != VM_TYPE_ROT) {
      do_error(st, "ERROR: %s not a vector or rotation\n",
	       expr->u.ident.name);
      return;
    }
    if(expr->u.ident.item != NULL) {
      if(expr->u.ident.item[0] == 0 || expr->u.ident.item[1] != 0 ||
	 ((expr->u.ident.item[0] < 'x' || expr->u.ident.item[0] > 'z') && 
	  expr->u.ident.item[0] != 's')) {
	do_error(st, "ERROR: Bad dotted identifier: %s.%s\n",
		 expr->u.ident.name, expr->u.ident.item);
	return;
      }
      int index = dotted_item_to_idx(expr->u.ident.item);
      if(index > 2 && expr->vtype == VM_TYPE_VECT) {
	do_error(st, "ERROR: %s not a rotation\n",
	       expr->u.ident.name);
	return;
      }
      expr->vtype = VM_TYPE_FLOAT;
    }
    break;
  case NODE_ASSIGN:
    if(expr->u.child[0]->node_type != NODE_IDENT) {
      // this is checked in the grammar, so this must be an assignment to a
      // named constant that has been converted by the parser
      assert(expr->u.child[0]->node_type == NODE_CONST);
      do_error(st, "ERROR: assignment to a constant\n"); return;
    }
    propagate_types(vasm, st, expr->u.child[0]); // finds variable's type
    if(st.error != 0) return;
    propagate_types(vasm, st, expr->u.child[1]);
    if(st.error != 0) return;
    expr->vtype = VM_TYPE_NONE /* expr->u.child[0]->vtype */; // FIXME
    // FIXME - do we really always want to auto-cast?
    expr->u.child[1] = enode_cast(expr->u.child[1], expr->u.child[0]->vtype);
    break;

  case NODE_ASSIGNADD:
  case NODE_ASSIGNSUB:
  case NODE_ASSIGNMUL:
  case NODE_ASSIGNDIV:
  case NODE_ASSIGNMOD:
    enode_split_assign(expr);
    assert(expr->node_type == NODE_ASSIGN);
    propagate_types(vasm, st, expr);
    return;
  case NODE_NEGATE:
    propagate_types(vasm, st, expr->u.child[0]);
    if(st.error != 0) return;
    update_loc(st, expr);
    expr->vtype = expr->u.child[0]->vtype;
    break;
  case NODE_VECTOR:
  case NODE_ROTATION:
    {
      float v[4]; bool is_const = true;
      int count = ( expr->node_type == NODE_VECTOR ? 3 : 4);
      for(int i = 0; i < count; i++) {
	propagate_types(vasm, st, expr->u.child[i]);
	if(st.error != 0) return;
	// FIXME - do we really always want to auto-cast?
	expr->u.child[i] = enode_cast(expr->u.child[i], VM_TYPE_FLOAT); 
      }
      update_loc(st, expr);

      for(int i = 0; i < count; i++) {
	if(expr->u.child[i]->node_type != NODE_CONST || 
	   expr->u.child[i]->vtype != VM_TYPE_FLOAT) {
	  is_const = false;
	};
      };
      expr->vtype = expr->node_type == NODE_VECTOR ? VM_TYPE_VECT : VM_TYPE_ROT;
      if(is_const) {
	for(int i = 0; i < count; i++) {
	  v[i] = expr->u.child[i]->u.f;
	  free(expr->u.child[i]); // FIXME - do this right!
	}
	expr->node_type = NODE_CONST;
	for(int i = 0; i < count; i++) expr->u.v[i] = v[i];
      } else {
	for(int i = 0; i < count; i++) {
	  propagate_types(vasm, st, expr->u.child[i]);
	  if(st.error != 0) return;	  
	}
      }
      break;
    }
  case NODE_ADD:
  case NODE_SUB:
  case NODE_MUL:
  case NODE_DIV:
  case NODE_MOD:
  case NODE_EQUAL:
  case NODE_NEQUAL:
  case NODE_LEQUAL:
  case NODE_GEQUAL:
  case NODE_LESS:
  case NODE_GREATER:
  case NODE_OR:
  case NODE_AND: 
  case NODE_XOR:
  case NODE_L_OR: // FIXME - think these need special handling for typecasts!
  case NODE_L_AND:
  case NODE_SHR:
  case NODE_SHL:
    propagate_types(vasm, st, expr->u.child[0]);
    if(st.error != 0) return;
    propagate_types(vasm, st, expr->u.child[1]);
    if(st.error != 0) return;
    update_loc(st, expr);

    insn = get_insn_binop(expr->node_type, expr->u.child[0]->vtype, 
			  expr->u.child[1]->vtype);
    if(insn == 0) {
      ltype = expr->u.child[0]->vtype;
      rtype = expr->u.child[1]->vtype;
      if(ltype == VM_TYPE_STR && rtype != VM_TYPE_STR) {
	insn = get_insn_binop(expr->node_type, ltype, VM_TYPE_STR);
	if(insn != 0)
	  expr->u.child[1] = enode_cast(expr->u.child[1], VM_TYPE_STR);
      } else if(ltype != VM_TYPE_STR && rtype == VM_TYPE_STR) {
	insn = get_insn_binop(expr->node_type, VM_TYPE_STR, rtype);
	if(insn != 0)
	  expr->u.child[0] = enode_cast(expr->u.child[0], VM_TYPE_STR);
      } else if(ltype == VM_TYPE_INT) {
	insn = get_insn_binop(expr->node_type, VM_TYPE_FLOAT, rtype);
	if(insn != 0)
	  expr->u.child[0] = enode_cast(expr->u.child[0], VM_TYPE_FLOAT);	
      } else if(rtype == VM_TYPE_INT) {
	insn = get_insn_binop(expr->node_type, ltype, VM_TYPE_FLOAT);
	if(insn != 0)
	  expr->u.child[1] = enode_cast(expr->u.child[1], VM_TYPE_FLOAT);	
      }
    }
    if((expr->node_type == NODE_EQUAL || expr->node_type == NODE_NEQUAL) &&
       expr->u.child[0]->vtype == VM_TYPE_LIST && 
       expr->u.child[1]->vtype == VM_TYPE_LIST) {
      // we don't bother with a special instruction for this; not worth it.
      expr->vtype = VM_TYPE_INT;
    } else if(insn != 0) {
      expr->vtype = get_insn_ret_type(insn);
    } else {
      do_error(st, "ERROR: bad types passed to operator %i %s : %s %s\n",
	       expr->node_type, node_names[expr->node_type],
	       type_names[ltype], type_names[rtype]); 
      return;
    }
    break;
    /* FIXME - need to implement a bunch of stuff */
  case NODE_NOT:
    propagate_types(vasm, st, expr->u.child[0]);
    if(st.error != 0) return;
    update_loc(st, expr);
    if(expr->u.child[0]->vtype != VM_TYPE_INT) {
      do_error(st, "ERROR: bitwise NOT on non-integer"); return;
    }
    expr->vtype = VM_TYPE_INT; 
    break;
  case NODE_L_NOT:
    propagate_types(vasm, st, expr->u.child[0]);
    if(st.error != 0) return;
    update_loc(st, expr);
    // no type enforcement, boolean context
    expr->vtype = VM_TYPE_INT; 
    break;
  case NODE_PREINC:
  case NODE_POSTINC:
  case NODE_PREDEC:
  case NODE_POSTDEC:
    propagate_types(vasm, st, expr->u.child[0]);
    if(st.error != 0) return;
    expr->vtype = expr->u.child[0]->vtype;
    break;
  case NODE_CAST:
    propagate_types(vasm, st, expr->u.child[0]);
    break;
  case NODE_CALL:
    {
      int arg_count = 0, i;
      for(lnode = expr->u.call.args; lnode != NULL; lnode = lnode->next) {
	propagate_types(vasm, st, lnode->expr);
	if(st.error != 0) return;
	arg_count++;
      }
      update_loc(st, expr);
      
      if(strcmp(expr->u.call.name,"print") == 0 && expr->u.call.args != NULL) {
	expr->vtype = VM_TYPE_NONE; // HACK! - FIXME remove this!
      } else {
	std::map<std::string, const vm_function*>::iterator iter =
	  st.funcs.find(expr->u.call.name);
	if(iter == st.funcs.end()) {
	  std::map<std::string, function*>::iterator sfiter = 
	    st.sys_funcs.find(expr->u.call.name);
	  
	  if(sfiter == st.sys_funcs.end()) {
	    do_error(st, "ERROR: call to unknown function %s\n", expr->u.call.name);
	    return;
	  } else {
	    st.funcs[expr->u.call.name] = make_function(vasm, sfiter->second);
	    iter = st.funcs.find(expr->u.call.name); // HACK.
	    assert(iter != st.funcs.end());
	  }
	}
	if(iter->second->arg_count != arg_count) {
	  do_error(st, "ERROR: wrong number of arguments to function %s\n", expr->u.call.name);
	  return;
	}
	i = 0;
	for(lnode = expr->u.call.args; lnode != NULL; lnode = lnode->next, i++) {
	  lnode->expr = arg_implicit_cast(st, lnode->expr, 
					  iter->second->arg_types[i]);
	  if(st.error != 0) return;
	}
	expr->vtype = iter->second->ret_type;
      }
    }
    break;
  case NODE_LIST:
    {
      for(lnode = expr->u.list; lnode != NULL; lnode = lnode->next) {
	propagate_types(vasm, st, lnode->expr);
	if(st.error != 0) return;
      }
      update_loc(st, expr);
    }
    break;
  }
#if 0
#define NODE_ASSIGNADD 22
#define NODE_ASSIGNSUB 23
#define NODE_ASSIGNMUL 24
#define NODE_ASSIGNDIV 25
#define NODE_ASSIGNMOD 26
 /* unary ops */
#define NODE_NEGATE 27
#define NODE_NOT 28 /* ~ - bitwise not */
#define NODE_L_NOT 29 /* ! - logical not */
#endif
}


static uint16_t get_insn_cast(uint8_t from_type, uint8_t to_type) {
  switch(MK_VM_TYPE_PAIR(from_type, to_type)) {
  case MK_VM_TYPE_PAIR(VM_TYPE_INT, VM_TYPE_NONE): return INSN_DROP_I;
  case MK_VM_TYPE_PAIR(VM_TYPE_FLOAT, VM_TYPE_NONE): return INSN_DROP_I;
  case MK_VM_TYPE_PAIR(VM_TYPE_VECT, VM_TYPE_NONE): return INSN_DROP_I3;
  case MK_VM_TYPE_PAIR(VM_TYPE_ROT, VM_TYPE_NONE): return INSN_DROP_I4;
  case MK_VM_TYPE_PAIR(VM_TYPE_INT, VM_TYPE_FLOAT): return INSN_CAST_I2F;
  case MK_VM_TYPE_PAIR(VM_TYPE_FLOAT, VM_TYPE_INT): return INSN_CAST_F2I;
  case MK_VM_TYPE_PAIR(VM_TYPE_INT, VM_TYPE_STR): return INSN_CAST_I2S;
  case MK_VM_TYPE_PAIR(VM_TYPE_FLOAT, VM_TYPE_STR): return INSN_CAST_F2S;
  case MK_VM_TYPE_PAIR(VM_TYPE_VECT, VM_TYPE_STR): return INSN_CAST_V2S;
  case MK_VM_TYPE_PAIR(VM_TYPE_ROT, VM_TYPE_STR): return INSN_CAST_R2S;
  case MK_VM_TYPE_PAIR(VM_TYPE_LIST, VM_TYPE_STR): return INSN_CAST_LIST2S;
  case MK_VM_TYPE_PAIR(VM_TYPE_INT, VM_TYPE_LIST): return INSN_CAST_I2L;
  case MK_VM_TYPE_PAIR(VM_TYPE_FLOAT, VM_TYPE_LIST): return INSN_CAST_F2L;
  case MK_VM_TYPE_PAIR(VM_TYPE_STR, VM_TYPE_LIST): return INSN_CAST_S2L;
  case MK_VM_TYPE_PAIR(VM_TYPE_KEY, VM_TYPE_LIST): return INSN_CAST_K2L;
  case MK_VM_TYPE_PAIR(VM_TYPE_VECT, VM_TYPE_LIST): return INSN_CAST_V2L;
  case MK_VM_TYPE_PAIR(VM_TYPE_ROT, VM_TYPE_LIST): return INSN_CAST_R2L;
  case MK_VM_TYPE_PAIR(VM_TYPE_STR, VM_TYPE_INT): return INSN_CAST_S2I;
  case MK_VM_TYPE_PAIR(VM_TYPE_STR, VM_TYPE_FLOAT): return INSN_CAST_S2F;
    
  /* FIXME - fill out the rest of these */
  default: return 0;
  }
}

// Remember, vectors are stored on the stack in normal x,y,z order, which means
// they're pushed on in reverse: z, then y, then x.
// To complicate matters, the local variable indexes we use in the compiler
// are the reverse of what you might expect: higher values mean lower addresses
static void read_var(vm_asm &vasm, lsl_compile_state &st, var_desc var, int index = -1) {
  assert(index < 0 || var.type == VM_TYPE_VECT || var.type == VM_TYPE_ROT);
  if(var.is_global) {
    switch(var.type) {
    case VM_TYPE_INT:
    case VM_TYPE_FLOAT:
      vasm.insn(MAKE_INSN(ICLASS_RDG_I, var.offset));
      break;
    case VM_TYPE_VECT:
      if(index < 0) {
	for(int i = 2; i >= 0; i--)
	  vasm.insn(MAKE_INSN(ICLASS_RDG_I, var.offset+i));
      } else {
	vasm.insn(MAKE_INSN(ICLASS_RDG_I, var.offset+index));
      }
      break;
    case VM_TYPE_ROT:
      if(index < 0) {
	for(int i = 3; i >= 0; i--)
	  vasm.insn(MAKE_INSN(ICLASS_RDG_I, var.offset+i));
      } else {
	vasm.insn(MAKE_INSN(ICLASS_RDG_I, var.offset+index));
      }
      break;
    case VM_TYPE_STR:
    case VM_TYPE_KEY:
    case VM_TYPE_LIST:
      vasm.insn(MAKE_INSN(ICLASS_RDG_P, var.offset));
      break;
    default:
      do_error(st,"FIXME: can't handle access to vars of type %s \n", 
	       type_names[var.type]);
      return;
    }
  } else {
    switch(var.type) {
    case VM_TYPE_INT:
    case VM_TYPE_FLOAT:
      vasm.rd_local_int(var.offset);
      break;
    case VM_TYPE_VECT:
      if(index < 0) {
	for(int i = 0; i < 3; i++)
	  vasm.rd_local_int(var.offset+i);
      } else {
	vasm.rd_local_int(var.offset+(2-index));
      }
      break;
    case VM_TYPE_ROT:
      if(index < 0) {
	for(int i = 0; i < 4; i++)
	  vasm.rd_local_int(var.offset+i);
      } else {
	vasm.rd_local_int(var.offset+(3-index));
      }
      break;
    case VM_TYPE_STR:
    case VM_TYPE_KEY:
    case VM_TYPE_LIST:
      vasm.rd_local_ptr(var.offset);
      break;
    default:
      do_error(st, "FIXME: can't handle access to vars of type %s \n", 
	       type_names[var.type]);
      return;
    }
  }
}

static void write_var(vm_asm &vasm, lsl_compile_state &st, var_desc var, int index = -1) {
  assert(index < 0 || var.type == VM_TYPE_VECT || var.type == VM_TYPE_ROT);
  if(var.is_global) {
    switch(var.type) {
    case VM_TYPE_INT:
    case VM_TYPE_FLOAT:
      vasm.insn(MAKE_INSN(ICLASS_WRG_I, var.offset));
      break;
    case VM_TYPE_VECT:
      if(index < 0) {
	for(int i = 0; i < 3; i++)
	  vasm.insn(MAKE_INSN(ICLASS_WRG_I, var.offset+i));
      } else {
	vasm.insn(MAKE_INSN(ICLASS_WRG_I, var.offset+index));
      }
      break;
    case VM_TYPE_ROT:
      if(index < 0) {
	for(int i = 0; i < 4; i++)
	  vasm.insn(MAKE_INSN(ICLASS_WRG_I, var.offset+i));
      } else {
	vasm.insn(MAKE_INSN(ICLASS_WRG_I, var.offset+index));
      }
      break;
    case VM_TYPE_STR:
    case VM_TYPE_KEY:
    case VM_TYPE_LIST:
      vasm.insn(MAKE_INSN(ICLASS_WRG_P, var.offset));
      break;
    default:
      do_error(st, "FIXME: can't handle access to vars of type %s \n", 
	       type_names[var.type]);
      return;
    }
  } else {
    switch(var.type) {
    case VM_TYPE_INT:
    case VM_TYPE_FLOAT:
      vasm.wr_local_int(var.offset); 
      break;
    case VM_TYPE_VECT:
      if(index < 0) {
	for(int i = 2; i >= 0; i--)
	  vasm.wr_local_int(var.offset+i);
      } else {
	vasm.wr_local_int(var.offset+(2-index));
      }
      break;
    case VM_TYPE_ROT:
      if(index < 0) {
	for(int i = 3; i >= 0; i--)
	  vasm.wr_local_int(var.offset+i);
      } else {
	vasm.wr_local_int(var.offset+(3-index));
      }
      break;
    case VM_TYPE_STR:
    case VM_TYPE_KEY:
    case VM_TYPE_LIST:
      vasm.wr_local_ptr(var.offset); // FIXME - TODO
      break;
    default:
      do_error(st, "FIXME: can't handle access to vars of type %s \n", 
	       type_names[var.type]);
      return;
    }
  }
}

// actually outputs a cast to boolean context
static void asm_cast_to_bool(vm_asm &vasm, lsl_compile_state &st, expr_node *expr) {
  switch(expr->vtype) {
  case VM_TYPE_INT:
    break;
  case VM_TYPE_FLOAT:
    vasm.insn(INSN_CAST_F2B); break;
  case VM_TYPE_STR:
    vasm.insn(INSN_CAST_S2B); break;
  case VM_TYPE_KEY:
    vasm.insn(INSN_CAST_K2B); break;
  case VM_TYPE_LIST:
    vasm.insn(INSN_CAST_L2B); break;
  case VM_TYPE_VECT:
    vasm.insn(INSN_CAST_V2B); break;
  case VM_TYPE_ROT:
    vasm.insn(INSN_CAST_R2B); break;
  default:
    do_error(st, "ERROR: can't cast %s type to bool\n", type_names[expr->vtype]);
  }
}

static void assemble_expr(vm_asm &vasm, lsl_compile_state &st, expr_node *expr) {
  uint8_t insn;
  update_loc(st, expr);
  switch(expr->node_type) {
  case NODE_CONST:
    switch(expr->vtype) {
    case VM_TYPE_INT:
      vasm.const_int(expr->u.i); break;
    case VM_TYPE_FLOAT:
      vasm.const_real(expr->u.f); break;
    case VM_TYPE_STR:
       vasm.const_str(expr->u.s); break;
    case VM_TYPE_VECT:
      for(int i = 2; i >= 0; i--)
	vasm.const_real(expr->u.v[i]);
      break;
    case VM_TYPE_ROT:
      for(int i = 3; i >= 0; i--)
	vasm.const_real(expr->u.v[i]);
      break;
    default:
      do_error(st, "FIXME: unhandled const type %s\n", type_names[expr->vtype]);
      return;
    }
    break;
  case NODE_IDENT:
    {
      var_desc var = get_variable(st, expr->u.ident.name);
      if(st.error != 0) return;
      if(expr->u.ident.item == NULL) {
	assert(var.type == expr->vtype);
	read_var(vasm, st, var);
      } else {
	assert(expr->vtype == VM_TYPE_FLOAT && 
	       (var.type == VM_TYPE_VECT || var.type == VM_TYPE_ROT));
	int offset = dotted_item_to_idx(expr->u.ident.item);
	assert(offset >= 0 && offset < (var.type == VM_TYPE_VECT ? 3 : 4));
	read_var(vasm, st, var, offset);
      }
    }
    break;
  case NODE_ASSIGN:
    {
      assert(expr->u.child[0]->node_type == NODE_IDENT); // checked in grammar
      uint8_t vtype = expr->u.child[1]->vtype; // FIXME - use child[0]?
      var_desc var = get_variable(st, expr->u.child[0]->u.ident.name);
      if(st.error != 0) return;
      if(expr->u.child[0]->u.ident.item == NULL) {
	assert(var.type == vtype);
      } else {
	assert(vtype == VM_TYPE_FLOAT && 
	       (var.type == VM_TYPE_VECT || var.type == VM_TYPE_ROT));
      }

      assemble_expr(vasm, st, expr->u.child[1]);
      if(st.error != 0) return;
      update_loc(st, expr);
      if(expr->u.child[0]->u.ident.item == NULL) {
	write_var(vasm, st, var);
      } else {
	int offset = dotted_item_to_idx(expr->u.child[0]->u.ident.item);
	assert(offset >= 0 && offset < (var.type == VM_TYPE_VECT ? 3 : 4));
	write_var(vasm, st, var, offset);
      }
    }
    break;
  case NODE_VECTOR:
  case NODE_ROTATION:
    {
      float v[4]; bool is_const = true;
      int count = ( expr->node_type == NODE_VECTOR ? 3 : 4);
      for(int i = count-1; i >= 0; i--) {
	// Note that this executes expressions in an odd order.
	// FIXME? (would slow down & complicate compiled code slightly)
	assemble_expr(vasm, st, expr->u.child[i]);
	if(st.error != 0) return;
	update_loc(st, expr);
      }
      break;
    }
  case NODE_NEGATE:
    assemble_expr(vasm, st, expr->u.child[0]);
    if(st.error != 0) return;
    update_loc(st, expr);
    switch(expr->vtype) {
    case VM_TYPE_INT:
      vasm.insn(INSN_NEG_I); break;
    case VM_TYPE_FLOAT:
      vasm.insn(INSN_NEG_F); break;
    case VM_TYPE_VECT:
      vasm.insn(INSN_NEG_V); break;
    case VM_TYPE_ROT:
      vasm.insn(INSN_NEG_R); break;
    default:
      do_error(st, "ERROR: bad type for unary - operator: %s\n",
		 type_names[expr->vtype]);
      return;
    }
    break;  
  case NODE_ADD:
  case NODE_SUB:
  case NODE_MUL:
  case NODE_DIV:
  case NODE_MOD:
  case NODE_EQUAL:
  case NODE_NEQUAL:
  case NODE_LEQUAL:
  case NODE_GEQUAL:
  case NODE_LESS:
  case NODE_GREATER:
  case NODE_OR:
  case NODE_AND:
  case NODE_XOR:
  case NODE_L_OR:
  case NODE_L_AND:
  case NODE_SHR:
  case NODE_SHL:
    {
      bool list_magic = false;
      insn = get_insn_binop(expr->node_type, expr->u.child[0]->vtype, 
			    expr->u.child[1]->vtype);
      if((expr->node_type == NODE_EQUAL || expr->node_type == NODE_NEQUAL) &&
	 expr->u.child[0]->vtype == VM_TYPE_LIST && 
	 expr->u.child[1]->vtype == VM_TYPE_LIST) {
	// we don't bother with a special instruction for this.
	list_magic = true; 
	insn = expr->node_type == NODE_EQUAL ? INSN_EQ_II : INSN_NEQ_II;
      } else if(insn == 0) {
	do_error(st, "INTERNAL ERROR: no insn for binop %i %s %s\n", 
		 expr->node_type, type_names[expr->u.child[0]->vtype],
		 type_names[expr->u.child[1]->vtype]);
	return;
      }
      assemble_expr(vasm, st, expr->u.child[0]);
      if(st.error != 0) return;
      if(list_magic) vasm.insn(INSN_LISTLEN);
      if(vasm.get_error() != NULL) {
	do_error(st, "[left] ASSEMBLER ERROR: %s\n", vasm.get_error());
	return;
      }
      assemble_expr(vasm, st, expr->u.child[1]);
      if(st.error != 0) return;
      if(list_magic) vasm.insn(INSN_LISTLEN);
      if(vasm.get_error() != NULL) {
	do_error(st, "[right] ASSEMBLER ERROR: %s\n", vasm.get_error());
	return;
      }
      update_loc(st, expr);
      vasm.insn(insn);
      break;    
    }
  case NODE_CALL:
    // HACK
    if(strcmp(expr->u.call.name,"print") == 0 && expr->u.call.args != NULL) {
      switch(expr->u.call.args->expr->vtype) {
      case VM_TYPE_INT:
	assemble_expr(vasm, st, expr->u.call.args->expr);
	if(st.error != 0) return;
	vasm.insn(INSN_PRINT_I);
	break;
      case VM_TYPE_FLOAT:
	assemble_expr(vasm, st, expr->u.call.args->expr);
	if(st.error != 0) return;
	vasm.insn(INSN_PRINT_F);
	break;
      case VM_TYPE_STR:
	assemble_expr(vasm, st, expr->u.call.args->expr);
	if(st.error != 0) return;
	vasm.insn(INSN_PRINT_STR);
	break;
      default:
	do_error(st, "ERROR: bad argument type to print() builtin: %s\n",
		 type_names[expr->u.call.args->expr->vtype]);
	return;
      }	
    } else {
      std::map<std::string, const vm_function*>::iterator iter =
	st.funcs.find(expr->u.call.name);
      if(iter == st.funcs.end()) {
	do_error(st, "ERROR: call to unknown function %s\n", expr->u.call.name);
	return;
      }

      if(iter->second->func_num == 0xffff) {
	// printf("DEBUG: beginning call to fake func %s\n", expr->u.call.name);
	for(list_node *lnode = expr->u.call.args; lnode != NULL; lnode = lnode->next) {
	  // printf("DEBUG: assembling an argument %p\n", lnode);
	  assemble_expr(vasm, st, lnode->expr);
	  if(st.error != 0) return;
	}
	update_loc(st, expr);
	// printf("DEBUG: doing call to fake func %s opcode %i\n",
	//        expr->u.call.name, (int)iter->second->insn_ptr);
	vasm.insn(iter->second->insn_ptr);
      } else {
	//printf("DEBUG: beginning call to %s\n", expr->u.call.name);
	vasm.begin_call(iter->second);
	for(list_node *lnode = expr->u.call.args; lnode != NULL; lnode = lnode->next) {
	  //printf("DEBUG: assembling an argument %p\n", lnode);
	  assemble_expr(vasm, st, lnode->expr);
	  if(st.error != 0) return;
	}

	update_loc(st, expr);
	//printf("DEBUG: doing call to %s\n", expr->u.call.name);
	vasm.do_call(iter->second);
      }
    }
    break;
  case NODE_NOT:
    assemble_expr(vasm, st, expr->u.child[0]);
    if(st.error != 0) return;
    update_loc(st, expr);
    vasm.insn(INSN_NOT_I);
    break;
  case NODE_L_NOT:
    assemble_expr(vasm, st, expr->u.child[0]);
    if(st.error != 0) return;
    update_loc(st, expr);
    asm_cast_to_bool(vasm, st, expr->u.child[0]);
    if(st.error) return;
    vasm.insn(INSN_NOT_L);
    break;
  case NODE_CAST:
    insn = get_insn_cast(expr->u.child[0]->vtype, expr->vtype);
    if(expr->u.child[0]->vtype == expr->vtype ||
       (expr->u.child[0]->vtype == VM_TYPE_STR && expr->vtype == VM_TYPE_KEY) ||
       (expr->u.child[0]->vtype == VM_TYPE_KEY && expr->vtype == VM_TYPE_STR)) {
      insn = INSN_NOOP;
    } else if(insn == 0) {
      do_error(st, "ERROR: couldn't cast %s -> %s\n", 
	       type_names[expr->u.child[0]->vtype], 
	       type_names[expr->vtype]);
      st.error = 1; return;
    }
    assemble_expr(vasm, st, expr->u.child[0]);
    if(st.error != 0) return;
    update_loc(st, expr);
    if(insn != INSN_NOOP)
      vasm.insn(insn);
    break;
  case NODE_PREINC:
  case NODE_PREDEC:
  case NODE_POSTINC:
  case NODE_POSTDEC:
    {
      assert(expr->u.child[0]->node_type == NODE_IDENT); // checked in grammar
      uint8_t vtype = expr->u.child[0]->vtype; 
      var_desc var = get_variable(st, expr->u.child[0]->u.s);
      if(st.error != 0) return;
      assert(var.type == vtype);
      int is_post = (expr->node_type == NODE_POSTINC || 
		     expr->node_type == NODE_POSTDEC);
      uint16_t insn, dup_insn;
      switch(vtype) {
      case VM_TYPE_INT:
	if(expr->node_type == NODE_PREINC || expr->node_type ==  NODE_POSTINC)
	  insn = INSN_INC_I;
	else insn = INSN_DEC_I;
	dup_insn = MAKE_INSN(ICLASS_RDL_I, 1);
	break;
      case VM_TYPE_FLOAT: // TODO
      default:
	do_error(st, "FIXME: can't handle increment of vars of type %s \n", 
		 type_names[vtype]);
	return;
      }
      read_var(vasm, st, var);
      // special magic in cast_to_void changes the node's vtype to NONE if
      // this is in a void context.
      if(is_post && expr->vtype != VM_TYPE_NONE) 
	vasm.insn(dup_insn);
      vasm.insn(insn);
      if(!is_post && expr->vtype != VM_TYPE_NONE) 
	vasm.insn(dup_insn);
      write_var(vasm, st, var);
    }
    break;
  case NODE_LIST:
    {
      if(expr->u.list == NULL) {
	vasm.empty_list(); // special case that should disappear
      } else if(expr->u.list->next == NULL && 
		expr->u.list->expr->node_type != NODE_CONST) {
	// another special case that should disappear
	expr_node *child = expr->u.list->expr;

	insn = get_insn_cast(child->vtype, VM_TYPE_LIST);
	if(child->vtype == VM_TYPE_LIST) {
	  insn = INSN_NOOP;
	} else if(insn == 0) {
	  do_error(st, "ERROR: couldn't cast %s to list\n", 
		   type_names[child->vtype]);
	  st.error = 1; return;
	}

	assemble_expr(vasm, st, child);
	if(st.error != 0) return;
	update_loc(st, expr);
	if(insn != INSN_NOOP)
	  vasm.insn(insn);
      } else {
	// TODO: handle non-constant lists (FIXME!)
	uint32_t ptr = constify_list(vasm, st, expr);
	if(st.error != 0) return;
	update_loc(st, expr);
	// TODO: merge identical list constants
	uint16_t global_const = vasm.add_global_ptr(ptr, VM_TYPE_LIST); 
	vasm.insn(MAKE_INSN(ICLASS_RDG_P, global_const));
      }
    }
    break;
  default:
    do_error(st, "FIXME: unhandled node type %i\n", expr->node_type);
    return;
  } 
  if(vasm.get_error() != NULL) {
    do_error(st, "ASSEMBLER ERROR: %s\n", vasm.get_error());
    return;
  }
}

static expr_node *cast_to_void(expr_node *expr) {
  // we're going to need magic here for assignments later, but for now...
  switch(expr->node_type) {
    // case NODE_ASSIGN: // will add once node can return something else
  case NODE_PREINC:
  case NODE_PREDEC:
  case NODE_POSTINC:
  case NODE_POSTDEC:
    // the code generator handles this specially
    expr->vtype = VM_TYPE_NONE; return expr;
  }

  return enode_cast(expr, VM_TYPE_NONE);
}

static void produce_code(vm_asm &vasm, lsl_compile_state &st, 
			 statement *statem, var_scope *scope) {
  for( ; statem != NULL; statem = statem->next) {
    st.scope = scope;
    switch(statem->stype) {
    case STMT_DECL:
      if(statem->expr[1] != NULL) {
	expr_node fake_expr;
	fake_expr.node_type = NODE_ASSIGN;
	fake_expr.u.child[0] = statem->expr[0];
	fake_expr.u.child[1] = statem->expr[1];
	propagate_types(vasm, st, &fake_expr);
	if(st.error) return;
	assert(fake_expr.vtype == VM_TYPE_NONE);
	assemble_expr(vasm, st, &fake_expr);
      }
      break; 
    case STMT_EXPR:
      propagate_types(vasm, st, statem->expr[0]);
      if(st.error) return;
      statem->expr[0] = cast_to_void(statem->expr[0]);
      assemble_expr(vasm, st, statem->expr[0]);
      break;
    case STMT_IF:
      {
	loc_atom else_cl = vasm.make_loc();
	loc_atom end_if = vasm.make_loc();
	propagate_types(vasm, st, statem->expr[0]); // FIXME - horrid code duplication
	if(st.error) return;
	assemble_expr(vasm, st, statem->expr[0]);
	if(st.error) return;
	asm_cast_to_bool(vasm, st, statem->expr[0]);
	if(st.error) return;
	vasm.insn(INSN_NCOND);
	vasm.do_jump(else_cl);
	if(vasm.get_error() != NULL) {
	  printf("ASSEMBLER ERROR: %s\n", vasm.get_error());
	  st.error = 1; return;
	}
	produce_code(vasm, st, statem->child[0], (var_scope*)statem->child_vars[0]);
	if(st.error) return;
	if(statem->child[1] != NULL) vasm.do_jump(end_if);
	vasm.do_label(else_cl);
	if(statem->child[1] != NULL) {
	  produce_code(vasm, st, statem->child[1], (var_scope*)statem->child_vars[1]);
	  if(st.error) return;
	  vasm.do_label(end_if);
	}
      }
      break;
    case STMT_RET:
      if(statem->expr[0] != NULL) {
	propagate_types(vasm, st, statem->expr[0]);
	if(st.error) return;
	// FIXME FIXME - check return type, do implicit cast
	
	assemble_expr(vasm, st, statem->expr[0]);
	if(st.error) return;
	switch(statem->expr[0]->vtype) {
	case VM_TYPE_INT:
	case VM_TYPE_FLOAT:
	  vasm.wr_local_int(0); break;
	case VM_TYPE_VECT:
	  for(int i = 2; i >= 0; i--)
	    vasm.wr_local_int(i);
	  break;
	case VM_TYPE_ROT: // FIXME - this doesn't look right!
	  for(int i = 3; i >= 0; i--)
	    vasm.wr_local_int(i);
	  break;
	case VM_TYPE_STR:
	  vasm.wr_local_ptr(0); break;
	default:
	  printf("FIXME: unhandled type in return statement\n");
	  st.error = 1; return;
	}
	
      }
      vasm.clear_stack();
      vasm.insn(INSN_RET);
      vasm.verify_stack(st.var_stack); // HACK until we have dead code elimination
      break;
    case STMT_WHILE:
      {
	loc_atom loop_start = vasm.make_loc();
	loc_atom loop_end = vasm.make_loc();
	vasm.do_label(loop_start);
	propagate_types(vasm, st, statem->expr[0]);
	if(st.error) return;
	assemble_expr(vasm, st, statem->expr[0]);
	if(st.error) return;
	asm_cast_to_bool(vasm, st, statem->expr[0]);
	if(st.error) return;
	vasm.insn(INSN_NCOND);
	vasm.do_jump(loop_end);
	if(vasm.get_error() != NULL) {
	  printf("ASSEMBLER ERROR: %s\n", vasm.get_error());
	  st.error = 1; return;
	}
	produce_code(vasm, st, statem->child[0], (var_scope*)statem->child_vars[0]);
	if(st.error) return;
	vasm.do_jump(loop_start);
	vasm.do_label(loop_end);
      }
      break;
    case STMT_DO:
      {
	loc_atom loop_start = vasm.make_loc();
	vasm.do_label(loop_start);
	if(vasm.get_error() != NULL) {
	  printf("ASSEMBLER ERROR: %s\n", vasm.get_error());
	  st.error = 1; return;
	}
	produce_code(vasm, st, statem->child[0], (var_scope*)statem->child_vars[0]);
	if(st.error) return;
	st.scope = scope;
	propagate_types(vasm, st, statem->expr[0]);
	if(st.error) return;
	//statem->expr[0] = enode_cast(statem->expr[0], VM_TYPE_INT); // FIXME - special bool cast?
	assemble_expr(vasm, st, statem->expr[0]);
	if(st.error) return;
	asm_cast_to_bool(vasm, st, statem->expr[0]);
	if(st.error) return;
	vasm.insn(INSN_COND);
	vasm.do_jump(loop_start);
      }
      break;
    case STMT_FOR:
      {
	loc_atom loop_start = vasm.make_loc();
	loc_atom loop_end = vasm.make_loc();
	if(statem->expr[0] != NULL) { // loop initialiser
	  propagate_types(vasm, st, statem->expr[0]);
	  if(st.error) return;
	  statem->expr[0] = cast_to_void(statem->expr[0]);
	  assemble_expr(vasm, st, statem->expr[0]);
	  if(st.error) return;
	}
	
	vasm.do_label(loop_start);
	if(statem->expr[1] != NULL) { // loop condition
	  propagate_types(vasm, st, statem->expr[1]);
	  if(st.error) return;
	  assemble_expr(vasm, st, statem->expr[1]);
	  if(st.error) return;
	  asm_cast_to_bool(vasm, st, statem->expr[1]);
	  if(st.error) return;
	  vasm.insn(INSN_NCOND);
	  vasm.do_jump(loop_end);
	  if(vasm.get_error() != NULL) {
	    printf("ASSEMBLER ERROR: %s\n", vasm.get_error());
	    st.error = 1; return;
	  }
	}
	
	produce_code(vasm, st, statem->child[0], (var_scope*)statem->child_vars[0]);
	if(st.error) return;
	st.scope = scope;

	if(statem->expr[2] != NULL) { // loop post-whatsit
	  propagate_types(vasm, st, statem->expr[2]);
	  if(st.error) return;
	  statem->expr[2] = cast_to_void(statem->expr[2]);
	  assemble_expr(vasm, st, statem->expr[2]);
	  if(st.error) return;
	}
	vasm.do_jump(loop_start); vasm.do_label(loop_end);
	if(vasm.get_error() != NULL) {
	    printf("ASSEMBLER ERROR: %s\n", vasm.get_error());
	    st.error = 1; return;
	}
	break;
      }
    case STMT_BLOCK:
      produce_code(vasm, st, statem->child[0], (var_scope*)statem->child_vars[0]);
      break;
    default:
      printf("ERROR: unhandled statement type %i\n", statem->stype);
	st.error = 1; return;
    }
  } 
}

// note - also used for runtime-provided funcs
static const vm_function *make_function(vm_asm &vasm, function *func, int state_no) {
  char *name = func->name;
  int num_args = 0, j = 0;
  for(func_arg* arg = func->args; arg != NULL; arg = arg->next) num_args++;
  uint8_t *args = new uint8_t[num_args];
  for(func_arg* arg = func->args; arg != NULL; arg = arg->next) 
    args[j++] = arg->vtype;
  
  if(state_no > -1) {
    // FIXME - this leaks memory (but then, so does everything)...
    int len = strlen(func->name)+10; name = (char*)malloc(len);
    snprintf(name, len, "%i:%s", state_no, func->name);
  }
  return  vasm.add_func(func->ret_type, args, num_args, name);
    
}

static void compile_function(vm_asm &vasm, lsl_compile_state &st, function *func, 
			     const vm_function *vfunc) {
    var_scope func_scope;
    vasm.begin_func(vfunc);

    handle_arg_vars(vasm, st, func->args, vfunc, &func_scope);
    if(st.error) return;
    
    extract_local_vars(vasm, st, func->code->first, &func_scope);
    if(st.error) return;

    st.var_stack = vasm.mark_stack();

    produce_code(vasm, st, func->code->first, &func_scope);

    if(st.error) return;
    vasm.verify_stack(st.var_stack);
    vasm.clear_stack();
    vasm.insn(INSN_RET);
    vasm.end_func();

    //st.vars.clear(); // not needed anymore.
  
    if(vasm.get_error() != NULL) {
      printf("ASSEMBLER ERROR: %s\n", vasm.get_error());
      st.error = 1; return;
    }
}

static uint32_t constify_list(vm_asm &vasm, lsl_compile_state &st, 
			      expr_node *expr) {
  int item_count = 0;
  assert(expr->node_type == NODE_LIST);
  update_loc(st, expr);
  for(list_node *item = expr->u.list; item != NULL; item = item->next) {
    item_count++;
    if(item->expr->node_type != NODE_CONST) {
      update_loc(st, item->expr);
      do_error(st, "List constant contains non-constant expression");
      return 0;
    }
  }

  vasm.begin_list();
  for(list_node *item = expr->u.list; item != NULL; item = item->next) {
    update_loc(st, item->expr);
    switch(item->expr->vtype) {
    case VM_TYPE_STR:
      vasm.list_add_str(item->expr->u.s);
      break;
    case VM_TYPE_INT:
      vasm.list_add_int(item->expr->u.i);
      break;
    case VM_TYPE_FLOAT:
      vasm.list_add_float(item->expr->u.f);
      break;
    case VM_TYPE_VECT:
      vasm.list_add_vect(item->expr->u.v);
      break;
    default:
      do_error(st, "Unhandled type %s in list\n", 
	       type_names[item->expr->vtype]);
      return 0;
    }
    if(vasm.get_error() != NULL) {
      do_error(st, "ASSEMBLER ERROR: %s\n", vasm.get_error());
      return 0;
    }
  }
  uint32_t retval = vasm.end_list();
  if(vasm.get_error() != NULL) {
    do_error(st, "ASSEMBLER ERROR: %s\n", vasm.get_error());
  }
  return retval;
}

int main(int argc, char** argv) {
  int num_funcs = 0; int func_no;
  vm_asm vasm;
  lsl_program *prog;
  lsl_compile_state st;
  st.error = 0;

  if(argc != 3) {
    printf("Usage: %s input.lsl output.cvm\n",argv[0]);
    return 1;
  }

  // First, we load the LSL runtime functions
  prog = caj_parse_lsl("runtime_funcs.lsl");
  if(prog == NULL) {
    printf("ERROR: couldn't parse function template\n"); return 1;
  }

  for(function *func = prog->funcs; func != NULL; func = func->next) {
    st.sys_funcs[func->name] = func;
  }

  // Now, we can do the actual compile!
  prog = caj_parse_lsl(argv[1]);
  if(prog == NULL) {
    printf(" *** Compile failed.\n"); return 1;
  }

  st.line_no = 0; st.column_no = 0;

  // The first step is to find all the global variables.
  for(global *g = prog->globals; g != NULL; g = g->next) {
    if(st.globals.count(g->name)) {
      printf("ERROR: duplicate definition of global var %s\n",g->name);
      st.error = 1; return 1;
    } else {
      var_desc var; var.type = g->vtype; var.is_global = 1;
      // FIXME - cast this, handle named constants.
      if(g->val != NULL) {
	propagate_types(vasm, st, g->val);
	if(g->val->node_type == NODE_LIST && g->vtype == VM_TYPE_LIST &&
	   g->val->vtype == g->vtype) {
	  // FIXME - TODO!
	} else if(g->val->node_type != NODE_CONST ||
			    g->val->vtype != g->vtype) {
	  printf("FIXME: global var initialiser not const of expected type\n");
	  printf("DEBUG: got %i %s node, wanted const %s\n",
		 g->val->node_type, type_names[g->val->vtype], type_names[g->vtype]);
	  st.error = 1; return 1;
	}
      }
      switch(g->vtype) {
      case VM_TYPE_INT:
	var.offset = vasm.add_global_int(g->val == NULL ? 0 : g->val->u.i); 
	break;
      case VM_TYPE_FLOAT:
	var.offset = vasm.add_global_float(g->val == NULL ? 0.0f : g->val->u.f); 
	break;
      case VM_TYPE_STR:
      case VM_TYPE_KEY:
	var.offset = vasm.add_global_ptr(vasm.add_string(g->val == NULL ? "" : g->val->u.s),
					 VM_TYPE_STR); 
	break;
      case VM_TYPE_LIST:
	if(g->val == NULL) {
	  printf("FIXME: can't do uninited lists yet\n");
	  st.error = 1; return 1;
	} else {
	  uint32_t ptr = constify_list(vasm, st, g->val);
	  if(st.error != 0) return 1;
	  var.offset = vasm.add_global_ptr(ptr, VM_TYPE_LIST); 
	}
	break;
      case VM_TYPE_VECT:
      case VM_TYPE_ROT:
	var.offset = vasm.add_global_float(g->val == NULL ? 0.0f : g->val->u.v[0]); 
	vasm.add_global_float(g->val == NULL ? 0.0f : g->val->u.v[1]); 
	vasm.add_global_float(g->val == NULL ? 0.0f : g->val->u.v[2]); 
	if(g->vtype == VM_TYPE_ROT)
	  vasm.add_global_float(g->val == NULL ? 0.0f : g->val->u.v[3]); 
	break;
      default:
	printf("ERROR: unknown type of global var %s\n",g->name);
	st.error = 1; return 1;
	// FIXME - handle this
      }
      assert(g->vtype == var.type);
      assert(var.type <= VM_TYPE_MAX); 
      st.globals[g->name] = var;
      
    }
  }

  // we want to make sure the default state is state 0
  lsl_state* dflt_state = NULL; int num_states = 1;
  st.states["default"] = 0;
  for(lsl_state *lstate = prog->states; lstate != NULL; lstate = lstate->next) {
    if(lstate->name == NULL) {
      if(dflt_state != NULL) {
	printf("ERROR: duplicate definition of default state\n"); return 1;
      }

      dflt_state = lstate;
    } else {
      if(st.states.count(lstate->name)) {
	printf("ERROR: duplicate definition of state %s\n", lstate->name); return 1;
      }

      st.states[lstate->name] = num_states++;
    }

    for(function *func = lstate->funcs; func != NULL; func = func->next)
      num_funcs++;
  }

  if(dflt_state == NULL) { printf("ERROR: no default state defined\n"); return 1; }

  // count the normal, non-event functions too...
  for(function *func = prog->funcs; func != NULL; func = func->next)
    num_funcs++;

  // Now we make a list of all functions - first the normal ones...
  const vm_function ** funcs = new const vm_function*[num_funcs]; // FIXME - use std::vector
  func_no = 0;
  for(function *func = prog->funcs; func != NULL; func = func->next) {
    if(st.funcs.count(func->name)) {
      printf("ERROR: duplicate definition of func %s\n", func->name);
      st.error = 1; return 1;
    }

    st.funcs[func->name] = funcs[func_no++] = make_function(vasm, func);
  }

  // ... and then the state events
  int state_ctr = 1;
  for(lsl_state *lstate = prog->states; lstate != NULL; lstate = lstate->next) {
    int state_no = lstate->name == NULL ? 0 : state_ctr++;

    // FIXME - this doesn't do type or duplicate checking!
    for(function *func = lstate->funcs; func != NULL; func = func->next) {
      funcs[func_no++] = make_function(vasm, func, state_no);
    }
  }

  // Insert the functions that are really instructions, if they aren't overridden
  for(int i = 0; op_funcs[i].name != NULL; i++) {
    if(st.funcs.count(op_funcs[i].name)) continue;
    // leaks memory horribly, as per usual
    vm_function *vfunc = new vm_function();
    vfunc->name = (char*)op_funcs[i].name;
    vfunc->ret_type = op_funcs[i].ret;
    vfunc->func_num = 0xffff; vfunc->insn_ptr = op_funcs[i].insn; // hack
    if(op_funcs[i].arg1 == VM_TYPE_NONE) {
      assert(op_funcs[i].arg2 == VM_TYPE_NONE);
      vfunc->arg_count = 0; vfunc->arg_types = NULL;
    } else if(op_funcs[i].arg2 == VM_TYPE_NONE) {
      vfunc->arg_count = 1; vfunc->arg_types = new uint8_t[1];
      vfunc->arg_types[0] = op_funcs[i].arg1;
    } else {
      vfunc->arg_count = 2; vfunc->arg_types = new uint8_t[2];
      vfunc->arg_types[0] = op_funcs[i].arg1; 
      vfunc->arg_types[1] = op_funcs[i].arg2;
    }
    st.funcs[op_funcs[i].name] = vfunc;
  }

  // now we build the code. It's important this is done in the same order as 
  // above, since the code here assumes this to be the case.
  func_no = 0;
  for(function *func = prog->funcs; func != NULL; func = func->next, func_no++) {
    // printf("DEBUG: assembling function %s\n", func->name);

    compile_function(vasm, st, func, funcs[func_no]);
    if(st.error != 0) return 1;
  }

  state_ctr = 1;
  for(lsl_state *lstate = prog->states; lstate != NULL; lstate = lstate->next) {
    int state_no = lstate->name == NULL ? 0 : state_ctr++;
    for(function *func = lstate->funcs; func != NULL; func = func->next, func_no++) {
      // printf("DEBUG: assembling state func %i:%s\n", state_no, func->name);
      compile_function(vasm, st, func, funcs[func_no]);
      if(st.error != 0) return 1;
    }
  }

  // finally, we serialise the whole thing and save it to disk
  size_t len;
  unsigned char *data = vasm.finish(&len);
  if(data == NULL) {
    printf("Error assembling: %s\n", vasm.get_error());
    return 1;
  }

  int fd = open(argv[2], O_WRONLY|O_CREAT|O_EXCL, 0644);
  if(fd < 0) {
    perror("opening output file");
    printf("Error: couldn't open output file!\n"); return 1;
  }
  size_t cnt = 0; ssize_t ret;
  while(cnt < len) {
    ret = write(fd, data+cnt, len-cnt);
    if(ret <= 0) {
      perror("writing output file"); return 1;
    }
    cnt += len;
  }
  free(data);  close(fd);
  
  return 0;
}
