#include "caj_lsl_parse.h"
#include "caj_vm.h"
#include "caj_vm_asm.h"
#include "caj_vm_exec.h" // FOR DEBUGGING
#include "caj_vm_ops.h"

#include <map>
#include <string>
#include <vector>
#include <cassert>
#include <stdio.h>
#include <stdarg.h>

// Possible Linden dain bramage:
// Order of operations (second operand, first operand)
// String/key typecasts: https://jira.secondlife.com/browse/SVC-1710
// ...

struct var_desc {
  uint8_t type; uint8_t is_global; uint16_t offset;
};

struct lsl_compile_state {
  int error; int line_no;
  std::map<std::string, var_desc> globals;
  std::map<std::string, var_desc> vars; // locals
  std::map<std::string, const vm_function*> funcs;
  loc_atom var_stack;
};

static void update_loc(lsl_compile_state &st, expr_node *expr) {
  st.line_no = expr->line_no;
  
}

static void do_error(lsl_compile_state &st, const char* format, ...) {
  va_list args;
  if(st.error != 0) return;
  printf("Line %i: ", st.line_no);
  va_start (args, format);
  vprintf (format, args);
  va_end (args);
  st.error = 1;
}

static void handle_arg_vars(vm_asm &vasm, lsl_compile_state &st,
			    func_arg *args, const vm_function* func) {
  for(int arg_no = 0; args != NULL; args = args->next, arg_no++) {
    if(st.vars.count(args->name)) {
      printf("ERROR: duplicate function argument %s\n",args->name);
      st.error = 1; return;
    } else {
      var_desc var; var.type = args->vtype; var.is_global = 0;
      var.offset = func->arg_offsets[arg_no];
      st.vars[args->name] = var;
      printf("DEBUG: added argument %s\n", args->name);
    }
  }
}

static void extract_local_vars(vm_asm &vasm, lsl_compile_state &st,
			       statement *statem) {
  for( ; statem != NULL; statem = statem->next) {
    if(statem->stype != STMT_DECL) continue;
    assert(statem->expr[0] != NULL && statem->expr[0]->node_type == NODE_IDENT);
    char* name = statem->expr[0]->u.s; uint8_t vtype = statem->expr[0]->vtype;
    if(st.vars.count(name)) {
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
      default:
	printf("ERROR: unknown type of local var %s\n",name);
	st.error = 1; return;
      // FIXME - handle this
      }
      printf("Adding local var %s %s\n", type_names[vtype], name);
      st.vars[name] = var;
    }
  }
}

static uint8_t get_insn_ret_type(uint16_t insn) {
  assert(insn < NUM_INSNS);
  return vm_insns[insn].ret;
}


static var_desc get_variable(lsl_compile_state &st, const char* name) {
  std::map<std::string, var_desc>::iterator iter = st.vars.find(name);
  if(iter == st.vars.end()) {
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

static void propagate_types(lsl_compile_state &st, expr_node *expr) {
  uint16_t insn; uint8_t ltype, rtype; list_node *lnode;
  update_loc(st, expr);
  switch(expr->node_type) {
  case NODE_CONST: break;
  case NODE_IDENT:
    // get_variable does all the nasty error handling for us!
    expr->vtype = get_variable(st, expr->u.s).type;
    break;
  case NODE_ASSIGN:
    assert(expr->u.child[0]->node_type == NODE_IDENT); // checked in grammar
    propagate_types(st, expr->u.child[0]); // finds variable's type
    if(st.error != 0) return;
    propagate_types(st, expr->u.child[1]);
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
    propagate_types(st, expr);
    return;
  case NODE_VECTOR:
  case NODE_ROTATION:
    {
      float v[4]; 
      int count = ( expr->node_type == NODE_VECTOR ? 3 : 4);
      for(int i = 0; i < count; i++) {
	propagate_types(st, expr->u.child[i]);
	if(st.error != 0) return;
	// FIXME - do we really always want to auto-cast?
	expr->u.child[i] = enode_cast(expr->u.child[i], VM_TYPE_FLOAT); 
      }
      update_loc(st, expr);

      for(int i = 0; i < count; i++) {
	if(expr->u.child[i]->node_type != NODE_CONST || 
	   expr->u.child[i]->vtype != VM_TYPE_FLOAT) {
	  do_error(st, "ERROR: %s not made up of constant floats\n",
		   expr->node_type == NODE_VECTOR ? "vector" : "rotation");
	  return;
	};
      };
      for(int i = 0; i < count; i++) {
	v[i] = expr->u.child[i]->u.f;
	free(expr->u.child[i]); // FIXME - do this right!
      }
      expr->vtype = expr->node_type == NODE_VECTOR ? VM_TYPE_VECT : VM_TYPE_ROT;
      expr->node_type = NODE_CONST;
      for(int i = 0; i < count; i++) expr->u.v[i] = v[i];
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
    propagate_types(st, expr->u.child[0]);
    if(st.error != 0) return;
    propagate_types(st, expr->u.child[1]);
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
    if(insn == 0) {
      do_error(st, "ERROR: bad types passed to operator %i %s : %s %s\n",
	       expr->node_type, node_names[expr->node_type],
	       type_names[ltype], type_names[rtype]); 
      return;
    }
    expr->vtype = get_insn_ret_type(insn);
    break;
    /* FIXME - need to implement a bunch of stuff */
  case NODE_NOT:
    propagate_types(st, expr->u.child[0]);
    if(st.error != 0) return;
    update_loc(st, expr);
    if(expr->u.child[0]->vtype != VM_TYPE_INT) {
      do_error(st, "ERROR: bitwise NOT on non-integer"); return;
    }
    expr->vtype = VM_TYPE_INT; 
    break;
  case NODE_L_NOT:
    propagate_types(st, expr->u.child[0]);
    if(st.error != 0) return;
    update_loc(st, expr);
    // no type enforcement, boolean context
    expr->vtype = VM_TYPE_INT; 
    break;
  case NODE_PREINC:
  case NODE_POSTINC:
  case NODE_PREDEC:
  case NODE_POSTDEC:
    propagate_types(st, expr->u.child[0]);
    if(st.error != 0) return;
    expr->vtype = expr->u.child[0]->vtype;
    break;
  case NODE_CAST:
    propagate_types(st, expr->u.child[0]);
    break;
  case NODE_CALL:
    /* FIXME - this is incomplete (doesn't verify args) */
    for(lnode = expr->u.call.args; lnode != NULL; lnode = lnode->next) {
      propagate_types(st, lnode->expr);
      if(st.error != 0) return;
    }
    update_loc(st, expr);

    if(strcmp(expr->u.call.name,"print") == 0 && expr->u.call.args != NULL) {
      expr->vtype = VM_TYPE_NONE; // HACK!
    } else {
      std::map<std::string, const vm_function*>::iterator iter =
	st.funcs.find(expr->u.call.name);
      if(iter == st.funcs.end()) {
	do_error(st, "ERROR: call to unknown function %s\n", expr->u.call.name);
	return;
      }
      expr->vtype = iter->second->ret_type;
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
  /* FIXME - fill out the rest of these */
  default: return 0;
  }
}

static void read_var(vm_asm &vasm, lsl_compile_state &st, var_desc var) {
  if(var.is_global) {
    switch(var.type) {
    case VM_TYPE_INT:
    case VM_TYPE_FLOAT:
      vasm.insn(MAKE_INSN(ICLASS_RDG_I, var.offset));
      break;
    case VM_TYPE_VECT:
      for(int i = 2; i >= 0; i--)
	vasm.insn(MAKE_INSN(ICLASS_RDG_I, var.offset+i));
      break;
    case VM_TYPE_STR:
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
    case VM_TYPE_STR:
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

static void write_var(vm_asm &vasm, lsl_compile_state &st, var_desc var) {
  if(var.is_global) {
    switch(var.type) {
    case VM_TYPE_INT:
    case VM_TYPE_FLOAT:
      vasm.insn(MAKE_INSN(ICLASS_WRG_I, var.offset));
      break;
    case VM_TYPE_STR:
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
    case VM_TYPE_STR:
    case VM_TYPE_LIST:
      // vasm.wr_local_ptr(var.offset); // FIXME - TODO
      // break;
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
    default:
      do_error(st, "FIXME: unhandled const type %s\n", type_names[expr->vtype]);
      return;
    }
    break;
  case NODE_IDENT:
    {
      var_desc var = get_variable(st, expr->u.s);
      if(st.error != 0) return;
      assert(var.type == expr->vtype);
      read_var(vasm, st, var);
    }
    break;
  case NODE_ASSIGN:
    {
      assert(expr->u.child[0]->node_type == NODE_IDENT); // checked in grammar
      uint8_t vtype = expr->u.child[1]->vtype; // FIXME - use child[0]?
      var_desc var = get_variable(st, expr->u.child[0]->u.s);
      if(st.error != 0) return;
      assert(var.type == vtype);

      assemble_expr(vasm, st, expr->u.child[1]);
      if(st.error != 0) return;
      update_loc(st, expr);
      write_var(vasm, st, var);
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
    insn = get_insn_binop(expr->node_type, expr->u.child[0]->vtype, 
			  expr->u.child[1]->vtype);
    if(insn == 0) {
      do_error(st, "INTERNAL ERROR: no insn for binop %i %s %s\n", 
	       expr->node_type, type_names[expr->u.child[0]->vtype],
	       type_names[expr->u.child[1]->vtype]);
      return;
    }
    assemble_expr(vasm, st, expr->u.child[0]);
    if(st.error != 0) return;
    assemble_expr(vasm, st, expr->u.child[1]);
    if(st.error != 0) return;
    update_loc(st, expr);
    vasm.insn(insn);
    break;    
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

      printf("DEBUG: beginning call to %s\n", expr->u.call.name);
      vasm.begin_call(iter->second);
      for(list_node *lnode = expr->u.call.args; lnode != NULL; lnode = lnode->next) {
	printf("DEBUG: assembling an argument %p\n", lnode);
	assemble_expr(vasm, st, lnode->expr);
	if(st.error != 0) return;
      }

      update_loc(st, expr);
      printf("DEBUG: doing call to %s\n", expr->u.call.name);
      vasm.do_call(iter->second);
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
    if(expr->u.child[0]->vtype == expr->vtype) {
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
      if(is_post && expr->vtype != VM_TYPE_NONE) 
	vasm.insn(dup_insn);
      vasm.insn(insn);
      if(!is_post && expr->vtype != VM_TYPE_NONE) 
	vasm.insn(dup_insn);
      write_var(vasm, st, var);
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
  case NODE_PREINC:
  case NODE_PREDEC:
  case NODE_POSTINC:
  case NODE_POSTDEC:
    // the code generator handles this specially
    expr->vtype = VM_TYPE_NONE; return expr;
  }

  return enode_cast(expr, VM_TYPE_NONE);
}

static void produce_code(vm_asm &vasm, lsl_compile_state &st, statement *statem) {
  for( ; statem != NULL; statem = statem->next) {
    switch(statem->stype) {
    case STMT_DECL:
      break; // FIXME!
    case STMT_EXPR:
      propagate_types(st, statem->expr[0]);
      if(st.error) return;
      statem->expr[0] = cast_to_void(statem->expr[0]);
      assemble_expr(vasm, st, statem->expr[0]);
      break;
    case STMT_IF:
      {
	loc_atom else_cl = vasm.make_loc();
	loc_atom end_if = vasm.make_loc();
	propagate_types(st, statem->expr[0]); // FIXME - horrid code duplication
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
	produce_code(vasm, st, statem->child[0]);
	if(st.error) return;
	if(statem->child[1] != NULL) vasm.do_jump(end_if);
	vasm.do_label(else_cl);
	if(statem->child[1] != NULL) {
	  produce_code(vasm, st, statem->child[1]);
	  if(st.error) return;
	  vasm.do_label(end_if);
	}
      }
      break;
    case STMT_RET:
      if(statem->expr[0] != NULL) {
	propagate_types(st, statem->expr[0]);
	if(st.error) return;
	// FIXME FIXME - check return type, do implicit cast
	
	assemble_expr(vasm, st, statem->expr[0]);
	if(st.error) return;
	switch(statem->expr[0]->vtype) {
	case VM_TYPE_INT:
	case VM_TYPE_FLOAT:
	  vasm.wr_local_int(0); break;
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
	propagate_types(st, statem->expr[0]);
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
	produce_code(vasm, st, statem->child[0]);
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
	produce_code(vasm, st, statem->child[0]);
	if(st.error) return;
	propagate_types(st, statem->expr[0]);
	if(st.error) return;
	statem->expr[0] = enode_cast(statem->expr[0], VM_TYPE_INT); // FIXME - special bool cast?
	assemble_expr(vasm, st, statem->expr[0]);
	if(st.error) return;
	vasm.insn(INSN_COND);
	vasm.do_jump(loop_start);
      }
      break;
    case STMT_BLOCK:
      produce_code(vasm, st, statem->child[0]);
      break;
    default:
      printf("ERROR: unhandled statement type %i\n", statem->stype);
	st.error = 1; return;
    }
  } 
}


int main(int argc, char** argv) {
  int num_funcs = 0; int func_no;
  vm_asm vasm;
  lsl_program *prog;
  lsl_compile_state st;
  st.error = 0;

  if(argc != 2) {
    printf("Usage: %s input.lsl\n",argv[0]);
    return 1;
  }

  prog = caj_parse_lsl(argv[1]);
  if(prog == NULL) {
    printf(" *** Compile failed.\n"); return 1;
  }

  for(global *g = prog->globals; g != NULL; g = g->next) {
    if(st.globals.count(g->name)) {
      printf("ERROR: duplicate definition of global var %s\n",g->name);
      st.error = 1; return 1;
    } else {
      var_desc var; var.type = g->vtype; var.is_global = 1;
      // FIXME - cast this, handle named constants.
      if(g->val != NULL) {
	propagate_types(st, g->val);
	if(g->val->node_type != NODE_CONST ||
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
	var.offset = vasm.add_global_ptr(vasm.add_string(g->val == NULL ? "" : g->val->u.s),
					 VM_TYPE_STR); 
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
      printf("Adding global var %s %s\n", type_names[g->vtype], g->name);
      assert(g->vtype == var.type); // FIXME - something funny is going on..
      assert(var.type <= VM_TYPE_MAX); 
      st.globals[g->name] = var;
      
    }
  }

  for(function *func = prog->funcs; func != NULL; func = func->next)
    num_funcs++;
  const vm_function ** funcs = new const vm_function*[num_funcs]; // FIXME - use std::vector
  func_no = 0;
  for(function *func = prog->funcs; func != NULL; func = func->next) {
    if(st.funcs.count(func->name)) {
      printf("ERROR: duplicate definition of func %s\n", func->name);
      st.error = 1; return 1;
    }

    int num_args = 0, j = 0;
    for(func_arg* arg = func->args; arg != NULL; arg = arg->next) num_args++;
    uint8_t *args = new uint8_t[num_args];
    for(func_arg* arg = func->args; arg != NULL; arg = arg->next) 
      args[j++] = arg->vtype;

    st.funcs[func->name] = funcs[func_no++] = 
      vasm.add_func(func->ret_type, args, num_args, func->name);
  }

  func_no = 0;
  for(function *func = prog->funcs; func != NULL; func = func->next, func_no++) {
    printf("DEBUG: assembling function %s\n", func->name);

    vasm.begin_func(funcs[func_no]);

    handle_arg_vars(vasm, st, func->args, funcs[func_no]);
    if(st.error) return 1;
    
    extract_local_vars(vasm, st, func->code->first);
    if(st.error) return 1;

    st.var_stack = vasm.mark_stack();

    produce_code(vasm, st, func->code->first);
    if(st.error) return 1;

    vasm.verify_stack(st.var_stack);
    vasm.clear_stack();
    vasm.insn(INSN_RET);
    vasm.end_func();

    st.vars.clear();
  
    if(vasm.get_error() != NULL) {
      printf("ASSEMBLER ERROR: %s\n", vasm.get_error());
      st.error = 1; return 1;
    }
  }

  size_t len;
  unsigned char *data = vasm.finish(&len);
  if(data == NULL) {
    printf("Error assembling: %s\n", vasm.get_error());
    return 1;
  }

  // HACK!
  printf("DEBUG: deserialising script\n");
  script_state* scr = vm_load_script(data, len);
  if(scr != NULL) {
    caj_vm_test(scr);
  } else { 
    printf("ERROR: deserialise failed, not running\n");
  }
  
  return 0;
}
