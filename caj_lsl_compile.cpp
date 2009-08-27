#include "caj_lsl_parse.h"
#include "caj_vm.h"
#include "caj_vm_asm.h"

#include <map>
#include <string>
#include <cassert>

struct var_desc {
  uint8_t type; uint16_t offset;
};

struct lsl_compile_state {
  int error;
  statement *func_code; 
  uint16_t var_offset; // FIXME - remove this & get from vm_asm
  std::map<std::string, var_desc> vars;
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
      var_desc var; var.type = vtype; var.offset = st.var_offset;
      // FIXME - initialise these where possible
      switch(vtype) {
      case VM_TYPE_INT:
	vasm.const_int(0); st.var_offset++;
	break;
      case VM_TYPE_FLOAT:
	vasm.const_real(0.0f); st.var_offset++;
	break;
      default:
	printf("ERROR: unknown type of local var %s\n",name);
	st.error = 1; return;
      // FIXME - handle this
      }
    }
  }
}

static uint8_t find_var_type(lsl_compile_state &st, const char* name) {
  std::map<std::string, var_desc>::iterator iter = st.vars.find(name);
  if(iter == st.vars.end()) return VM_TYPE_NONE;
  return iter->second.type;
}

static void propagate_types(lsl_compile_state &st, expr_node *expr);

static void fix_binop_types(lsl_compile_state &st, expr_node *&left, expr_node *&right) {
    propagate_types(st, left);
    propagate_types(st, right);
    if(left->vtype == VM_TYPE_FLOAT && right->vtype == VM_TYPE_INT) {
      right = enode_cast(right, VM_TYPE_FLOAT);
    } else if(left->vtype == VM_TYPE_INT && right->vtype == VM_TYPE_FLOAT) {
      left = enode_cast(left, VM_TYPE_FLOAT);
    }
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
    
  default:
    break;
  }
  return 0;
}

static void propagate_types(lsl_compile_state &st, expr_node *expr) {
  switch(expr->node_type) {
  case NODE_CONST: break;
  case NODE_IDENT:
    expr->vtype = find_var_type(st, expr->u.s);
    if(expr->vtype == VM_TYPE_NONE) {
      printf("ERROR: Reference to unknown var %s\n");
      st.error = 1; return;
    }
    break;
  case NODE_ADD:
  case NODE_SUB:
    fix_binop_types(st, expr->u.child[0], expr->u.child[1]);
    if(expr->node_type == NODE_ADD && 
       (expr->u.child[0]->vtype == VM_TYPE_STR || 
	expr->u.child[1]->vtype == VM_TYPE_STR)) {
      expr->u.child[0] = enode_cast(expr->u.child[0], VM_TYPE_STR);
      expr->u.child[1] = enode_cast(expr->u.child[1], VM_TYPE_STR);
    }
    expr->vtype = expr->u.child[0]->vtype;
    if(expr->vtype != expr->u.child[1]->vtype) {
      printf("ERROR: Type mismatch %s %s\n",
	     type_names[expr->vtype], type_names[expr->u.child[1]->vtype]);
      st.error = 1; return;
    };
    break;
  case NODE_MUL:
  case NODE_DIV:
    fix_binop_types(st, expr->u.child[0], expr->u.child[1]);
    expr->vtype = expr->u.child[0]->vtype; // FIXME!!!
    break;
  }
#if 0
#define NODE_MUL 5
#define NODE_DIV 6
#define NODE_MOD 7
#define NODE_ASSIGN 8
#define NODE_EQUAL 9
#define NODE_NEQUAL 10
#define NODE_LEQUAL 11
#define NODE_GEQUAL 12
#define NODE_LESS 13
#define NODE_GREATER 14
#define NODE_OR 15
#define NODE_AND 16
#define NODE_XOR 17
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



static void produce_code(vm_asm &vasm, lsl_compile_state &st) {
  for(statement *statem = st.func_code; statem != NULL; statem = statem->next) {
    switch(statem->stype) {
    case STMT_DECL:
      break; // FIXME!
    case STMT_EXPR:
      propagate_types(st, statem->expr[0]);
      if(st.error) return;
      // TODO - FIXME
      break;
    default:
      printf("ERROR: unhandled statement type %i\n", statem->stype);
	st.error = 1; return;
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
    st.var_offset = 0;
    vasm.begin_func(NULL, 0); // FIXME
    st.func_code = func->code->first;
    extract_local_vars(vasm, st);
    if(st.error) return 1;
    produce_code(vasm, st);
  }
  
  return 0;
}
