#include "caj_lsl_parse.h"
#include "caj_vm.h"
#include "caj_vm_asm.h"
#include "caj_vm_exec.h" // FOR DEBUGGING

#include <map>
#include <string>
#include <vector>
#include <cassert>

struct var_desc {
  uint8_t type; uint16_t offset;
};

struct lsl_compile_state {
  int error;
  statement *func_code; 
  std::map<std::string, var_desc> vars;
  std::vector<uint8_t> stack_vars;
};

static void extract_local_vars(vm_asm &vasm, lsl_compile_state &st) {
  for(statement *statem = st.func_code; statem != NULL; statem = statem->next) {
    if(statem->stype != STMT_DECL) continue;
    assert(statem->expr[0] != NULL && statem->expr[0]->node_type == NODE_IDENT);
    char* name = statem->expr[0]->u.s; uint8_t vtype = statem->expr[0]->vtype;
    if(st.vars.count(name)) {
      printf("ERROR: duplicate definition of local var %s\n",name);
      st.error = 1; return;
      // FIXME - handle this
    } else {
      var_desc var; var.type = vtype; var.offset = st.stack_vars.size();
      // FIXME - initialise these where possible
      switch(vtype) {
      case VM_TYPE_INT:
	st.stack_vars.push_back(vtype);
	vasm.const_int(0); 
	break;
      case VM_TYPE_FLOAT:
	st.stack_vars.push_back(vtype);
	vasm.const_real(0.0f); 
	break;
      default:
	printf("ERROR: unknown type of local var %s\n",name);
	st.error = 1; return;
      // FIXME - handle this
      }
      st.vars[name] = var;
    }
  }
}

static uint8_t find_var_type(lsl_compile_state &st, const char* name) {
  std::map<std::string, var_desc>::iterator iter = st.vars.find(name);
  if(iter == st.vars.end()) return VM_TYPE_NONE;
  return iter->second.type;
}


static uint16_t get_insn_binop(int node_type, uint8_t ltype, uint8_t rtype) {
  switch(node_type) {
  case NODE_ADD:
    if(ltype != rtype) return 0;
    switch(ltype) {
    case VM_TYPE_INT: return INSN_ADD_II;
    case VM_TYPE_FLOAT: return INSN_ADD_FF;
    case VM_TYPE_STR: return 0; // TODO - FIXME
    default: return 0;
    }
    break;
  case NODE_SUB:
    if(ltype != rtype) return 0;
    switch(ltype) {
    case VM_TYPE_INT: return INSN_SUB_II;
    case VM_TYPE_FLOAT: return INSN_SUB_FF;
    default: return 0;
    }
    break;
  case NODE_MUL:
    if(ltype != rtype) return 0;
    switch(ltype) {
    case VM_TYPE_INT: return INSN_MUL_II;
    case VM_TYPE_FLOAT: return INSN_MUL_FF;
    default: return 0;
    }
    break;
  case NODE_DIV:
    if(ltype != rtype) return 0;
    switch(ltype) {
    case VM_TYPE_INT: return INSN_DIV_II;
    case VM_TYPE_FLOAT: return INSN_DIV_FF;
    default: return 0;
    }
    break;
  case NODE_MOD:
    if(ltype != rtype) return 0;
    switch(ltype) {
    case VM_TYPE_INT: return INSN_MOD_II;
      /* case VM_TYPE_VECTOR: TODO - dot product */
    default: return 0;
    }
  /* TODO - comparison operators */
  case NODE_AND:
    if(ltype == VM_TYPE_INT && rtype == VM_TYPE_INT) return INSN_AND_II;
    else return 0;
  case NODE_OR:
    if(ltype == VM_TYPE_INT && rtype == VM_TYPE_INT) return INSN_OR_II;
    else return 0;
  case NODE_XOR:
    if(ltype == VM_TYPE_INT && rtype == VM_TYPE_INT) return INSN_XOR_II;
    else return 0;
  default:
    break;
  }
  return 0;
}

static uint8_t get_insn_ret_type(uint16_t insn) {
  assert(insn < NUM_INSNS);
  return vm_insns[insn].ret;
}

static void propagate_types(lsl_compile_state &st, expr_node *expr) {
  uint16_t insn; uint8_t ltype, rtype; list_node *lnode;
  switch(expr->node_type) {
  case NODE_CONST: break;
  case NODE_IDENT:
    expr->vtype = find_var_type(st, expr->u.s);
    if(expr->vtype == VM_TYPE_NONE) {
      printf("ERROR: Reference to unknown var %s\n", expr->u.s);
      st.error = 1; return;
    }
    break;
  case NODE_ASSIGN:
    assert(expr->u.child[0]->node_type == NODE_IDENT); // checked in grammar
    propagate_types(st, expr->u.child[0]); // finds variable's type
    if(st.error != 0) return;
    propagate_types(st, expr->u.child[1]);
    if(st.error != 0) return;
    expr->vtype = expr->u.child[0]->vtype;
    // FIXME - do we really always want to auto-cast?
    expr->u.child[1] = enode_cast(expr->u.child[1], expr->vtype);
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
    propagate_types(st, expr->u.child[0]);
    if(st.error != 0) return;
    propagate_types(st, expr->u.child[1]);
    if(st.error != 0) return;
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
      printf("ERROR: bad types passed to operator %i: %s %s\n",
	     expr->node_type, type_names[ltype], type_names[rtype]); 
      st.error = 1; return;
    }
    expr->vtype = get_insn_ret_type(insn);
    break;
    /* FIXME - need to implement a bunch of stuff */
  case NODE_CALL:
    /* FIXME - this is incomplete */
    for(lnode = expr->u.call.args; lnode != NULL; lnode = lnode->next) {
      propagate_types(st, lnode->expr);
    }
  }
#if 0
#define NODE_ASSIGN 8
#define NODE_L_OR 18
#define NODE_L_AND 19
#define NODE_SHR 20
#define NODE_SHL 21
#define NODE_ASSIGNADD 22
#define NODE_ASSIGNSUB 23
#define NODE_ASSIGNMUL 24
#define NODE_ASSIGNDIV 25
#define NODE_ASSIGNMOD 26
 /* unary ops */
#define NODE_NEGATE 27
#define NODE_NOT 28 /* ~ - bitwise not */
#define NODE_L_NOT 29 /* ! - logical not */
#define NODE_PREINC 30 /* ++foo */
#define NODE_POSTINC 31 /* foo++ */
#define NODE_PREDEC 32 /* --foo */
#define NODE_POSTDEC 33 /* foo-- */
#endif
}

/* WARNING WARING - this needs fixing if we increase the number of types */
#define TYPE_PAIR(a, b) (((a) << 3) | b)

static uint16_t get_insn_cast(uint8_t from_type, uint8_t to_type) {
  switch(TYPE_PAIR(from_type, to_type)) {
  case TYPE_PAIR(VM_TYPE_INT, VM_TYPE_FLOAT): return INSN_CAST_I2F;
  case TYPE_PAIR(VM_TYPE_FLOAT, VM_TYPE_INT): return INSN_CAST_F2I;
  /* FIXME - fill out the rest of these */
  default: return 0;
  }
}

static void assemble_expr(vm_asm &vasm, lsl_compile_state &st, expr_node *expr) {
  uint8_t insn;
  switch(expr->node_type) {
  case NODE_CONST:
    switch(expr->vtype) {
    case VM_TYPE_INT:
      printf("DEBUG: assembling int literal\n");
      vasm.const_int(expr->u.i); break;
    case VM_TYPE_FLOAT:
     printf("DEBUG: assembling float literal\n");
      vasm.const_real(expr->u.f); break;
    default:
      printf("FIXME: unhandled const type %s\n", type_names[expr->vtype]);
      st.error = 1; return;
    }
    break;
  case NODE_IDENT:
    {
      uint16_t var_id;
      std::map<std::string, var_desc>::iterator iter = st.vars.find(expr->u.s);
      if(iter == st.vars.end()) {
	printf("INTERNAL ERROR: missing variable %s\n", expr->u.s);
	st.error = 1; return;
      }
      assert(iter->second.type == expr->vtype);
      var_id = iter->second.offset;

      switch(expr->vtype) {
      case VM_TYPE_INT:
      case VM_TYPE_FLOAT:
	vasm.rd_local_int(var_id);
	break;
      default:
	printf("FIXME: can't handle access to vars of type %s \n", 
	       type_names[expr->vtype]);
	st.error = 1; return;
      }
    }
    break;
  case NODE_ASSIGN:
    {
      uint16_t var_id;
      assert(expr->u.child[0]->node_type == NODE_IDENT); // checked in grammar
      assert(expr->vtype == expr->u.child[1]->vtype); // ensured by type propagation
      std::map<std::string, var_desc>::iterator iter = st.vars.find(expr->u.child[0]->u.s);
      if(iter == st.vars.end()) {
	printf("INTERNAL ERROR: missing variable %s\n", expr->u.s);
	st.error = 1; return;
      }
      assert(iter->second.type == expr->vtype);
      var_id = iter->second.offset;
      assemble_expr(vasm, st, expr->u.child[1]);
      if(st.error != 0) return;
      switch(expr->vtype) {
      case VM_TYPE_INT:
      case VM_TYPE_FLOAT:
	vasm.wr_local_int(var_id);
	break;
      default:
	printf("FIXME: can't handle assignment to vars of type %s \n", 
	       type_names[expr->vtype]);
	st.error = 1; return;
      }
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
    insn = get_insn_binop(expr->node_type, expr->u.child[0]->vtype, 
			  expr->u.child[1]->vtype);
    if(insn == 0) {
      printf("INTERNAL ERROR: no insn for binop %i %s %s\n", 
	     expr->node_type, type_names[expr->u.child[0]->vtype],
	     type_names[expr->u.child[1]->vtype]);
      st.error = 1; return;
    }
    assemble_expr(vasm, st, expr->u.child[0]);
    if(st.error != 0) return;
    assemble_expr(vasm, st, expr->u.child[1]);
    if(st.error != 0) return;
    printf("DEBUG: assembling binary op\n");
    vasm.insn(insn);
    break;    
  case NODE_CALL:
    // HACK
    if(strcmp(expr->u.call.name,"print") == 0 && expr->u.call.args != NULL) {
      switch(expr->u.call.args->expr->vtype) {
      case VM_TYPE_INT:
	assemble_expr(vasm, st, expr->u.call.args->expr);
	if(st.error != 0) return;
	printf("DEBUG: assembling PRINT_I\n");
	vasm.insn(INSN_PRINT_I);
	break;
      case VM_TYPE_FLOAT:
	assemble_expr(vasm, st, expr->u.call.args->expr);
	if(st.error != 0) return;
	printf("DEBUG: assembling PRINT_F\n");
	vasm.insn(INSN_PRINT_F);
	break;
      default:
	printf("ERROR: bad argument type to print() builtin: %s\n",
	       type_names[expr->u.call.args->expr->vtype]);
	st.error = 1; return;
      }	
    } else {
      printf("ERROR: procedure calls not done yet\n");
	st.error = 1; return;
    }
    break;
  case NODE_CAST:
    insn = get_insn_cast(expr->u.child[0]->vtype, expr->vtype);
    if(expr->u.child[0]->vtype == expr->vtype) {
      printf("WARNING: got cast to same type. Continuing anyway\n");
      insn = INSN_NOOP;
    } else if(insn == 0) {
      printf("ERROR: couldn't cast %s -> %s\n", 
	     type_names[expr->u.child[0]->vtype], 
	     type_names[expr->vtype]);
      st.error = 1; return;
    }
    assemble_expr(vasm, st, expr->u.child[0]);
    if(st.error != 0) return;
    vasm.insn(insn);
    break;
  default:
    printf("FIXME: unhandled node type %i\n", expr->node_type);
    st.error = 1; return;
  } 
  if(vasm.get_error() != NULL) {
    printf("ASSEMBLER ERROR: %s\n", vasm.get_error());
    st.error = 1; return;
  }
}

static void produce_code(vm_asm &vasm, lsl_compile_state &st) {
  for(statement *statem = st.func_code; statem != NULL; statem = statem->next) {
    switch(statem->stype) {
    case STMT_DECL:
      break; // FIXME!
    case STMT_EXPR:
      propagate_types(st, statem->expr[0]);
      if(st.error) return;
      // FIXME - need to cast expression to void!
      assemble_expr(vasm, st, statem->expr[0]);
      break;
    default:
      printf("ERROR: unhandled statement type %i\n", statem->stype);
	st.error = 1; return;
    }
  } 
}

static void clear_stack(vm_asm &vasm, lsl_compile_state &st) {
  for(int i = st.stack_vars.size()-1; i >= 0; i--) {
    switch(st.stack_vars[i]) {
    case VM_TYPE_INT:
    case VM_TYPE_FLOAT:
      printf("DEBUG: stack clear: assembling POP_I\n");
      vasm.insn(INSN_POP_I);
      break;
    }
  }
}

int main(int argc, char** argv) {
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

  for(function *func = prog->funcs; func != NULL; func = func->next) {
    printf("DEBUG: assembling function %s\n", func->name);
    vasm.begin_func(NULL, 0); // FIXME
    st.func_code = func->code->first;
    extract_local_vars(vasm, st);
    if(st.error) return 1;
    produce_code(vasm, st);
    if(st.error) return 1;
    clear_stack(vasm,st);
    vasm.insn(INSN_RET);
    vasm.end_func();
    if(vasm.get_error() != NULL) {
      printf("ASSEMBLER ERROR: %s\n", vasm.get_error());
      st.error = 1; return 1;
    }
  }

  script_state *scr = vasm.finish();
  if(scr == NULL) {
    printf("Error assembling: %s\n", vasm.get_error());
    return 1;
  }
  printf("DEBUG: testing code execution\n");
  caj_vm_test(scr); // FIXME - HACK HACK HACK
  
  return 0;
}
