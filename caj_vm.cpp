/* Copyright (c) 2009 Aidan Thornton, all rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AIDAN THORNTON ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL AIDAN THORNTON BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF 
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "caj_vm.h"
#include "caj_vm_asm.h"

static void step_script(script_state* st, int num_steps) {
  uint16_t* bytecode = st->bytecode;
  int32_t* stack_top = st->stack_top;
  uint32_t ip = st->ip;
  for( ; num_steps > 0 && ip != 0; num_steps--) {
    // printf("DEBUG: executing at %u\n", ip);
    uint16_t insn = bytecode[ip++];
    int ival;
    switch(GET_ICLASS(insn)) {
    case ICLASS_NORMAL:
      switch(GET_IVAL(insn)) {
      case INSN_NOOP:
	break;
      case INSN_ABORT:
	goto abort_exec;
      case INSN_ADD_II:
	stack_top[2] = stack_top[2] + stack_top[1];
	stack_top++;
	break;
      case INSN_SUB_II:
	stack_top[2] = stack_top[2] - stack_top[1];
	stack_top++;
	break;
      case INSN_MUL_II:
	stack_top[2] = stack_top[2] * stack_top[1];
	stack_top++;
	break;
      case INSN_DIV_II:
	if(unlikely(stack_top[1] == 0)) goto abort_exec;
	stack_top[2] = stack_top[2] / stack_top[1];
	stack_top++;
	break;
      case INSN_ADD_FF:
	((float*)stack_top)[2] = ((float*)stack_top)[2] + ((float*)stack_top)[1];
	stack_top++;
	break;
      case INSN_SUB_FF:
	((float*)stack_top)[2] = ((float*)stack_top)[2] - ((float*)stack_top)[1];
	stack_top++;
	break;
      case INSN_MUL_FF:
	((float*)stack_top)[2] = ((float*)stack_top)[2] * ((float*)stack_top)[1];
	stack_top++;
	break;
      case INSN_DIV_FF:
	((float*)stack_top)[2] = ((float*)stack_top)[2] / ((float*)stack_top)[1];
	stack_top++;
	break;
      case INSN_RET:
	ip = *(++stack_top); 
	break;
      case INSN_MOD_II:
	if(unlikely(stack_top[1] == 0)) goto abort_exec;
	stack_top[2] = stack_top[2] % stack_top[1];
	stack_top++;
	break;
      // TODO - implement bitwise operators!
      case INSN_AND_L:
	stack_top[2] = stack_top[2] && stack_top[1];
	stack_top++;
	break;
      case INSN_OR_L:
	stack_top[2] = stack_top[2] || stack_top[1];
	stack_top++;
	break;
      case INSN_NOT_L:
	stack_top[1] = !stack_top[1];
	break;	
      case INSN_COND:
	if(*(++stack_top) == 0) ip++;
	break;
      case INSN_NCOND:
	if(*(++stack_top) != 0) ip++;
	break;
      case INSN_EQ_II:
	stack_top[2] = stack_top[2] == stack_top[1];
	stack_top++;
	break;
      case INSN_NEQ_II:
	stack_top[2] = stack_top[2] != stack_top[1];
	stack_top++;
	break;
      case INSN_GR_II:
	stack_top[2] = stack_top[2] > stack_top[1];
	stack_top++;
	break;
      case INSN_LES_II:
	stack_top[2] = stack_top[2] < stack_top[1];
	stack_top++;
	break;
      case INSN_GEQ_II:
	stack_top[2] = stack_top[2] >= stack_top[1];
	stack_top++;
	break;
      case INSN_LEQ_II:
	stack_top[2] = stack_top[2] <= stack_top[1];
	stack_top++;
	break;
      case INSN_POP_I:
	stack_top++; break;
      case INSN_QUIT:
	ip = 0; goto out;
      case INSN_PRINT_I:
	printf("DEBUG: int %i\n", (int)*(++stack_top));
	break;
      case INSN_PRINT_F:
	printf("DEBUG: float %f\n", (double)*(float*)(++stack_top));
	break;
      case INSN_PRINT_STR:
	{
	  int32_t tmp = *(++stack_top);
	  int32_t len = st->heap[tmp+1];
	  char buf[len+1]; 
	  memcpy(buf,&st->heap[tmp+2],len);
	  buf[len] = 0;
	  printf("DEBUG: string '%s'\n", buf);
	  st->heap[tmp]--;
	}
      default:
	goto abort_exec;
      }
      break;
    case ICLASS_JUMP:
      {
	int16_t ival = GET_IVAL(insn);
	if(ival & 0x800) {
	  ip -= ival & 0x7ff;
	} else {
	  ip += ival;
	}
      }
      break;
    case ICLASS_RDG_I:
      *(stack_top--) = st->globals[GET_IVAL(insn)];
      break;
    case ICLASS_WRG_I:
      st->globals[GET_IVAL(insn)] = *(++stack_top);
      break;
    case ICLASS_RDG_P:
      {
	int32_t tmp = st->globals[GET_IVAL(insn)];
	st->heap[tmp]++;
	*(stack_top--) = tmp;
      }
    // TODO - other global-related instructions
    case ICLASS_RDL_I:
      *stack_top = stack_top[GET_IVAL(insn)];
      stack_top--;
      break;
    case ICLASS_WRL_I:
      // FIXME - is this where we want to do the offset from
      stack_top++;
      stack_top[GET_IVAL(insn)] = *stack_top;
      break;
    default:
      goto abort_exec;
    }
  }
 out:
  st->stack_top = stack_top;
  st->ip = ip;
  return; // FIXME;
 abort_exec:
  st->ip = 0; st->scram_flag = 1;
}

int main(void) {
#if 1
  // Test function - calculates the GCD of 1071 and 462
  
  uint8_t arg_types[2]; 
  arg_types[0] = VM_TYPE_INT;
  arg_types[1] = VM_TYPE_INT;
  
  vm_asm vasm;
  vasm.begin_func(arg_types, 2);
  loc_atom start_lab = vasm.make_loc();
  loc_atom ret_lab = vasm.make_loc();
  vasm.do_label(start_lab); // label start
  // Right now, stack looks like [TOP] b a
  vasm.rd_local_int(1); // b
  vasm.insn(INSN_NCOND);
  vasm.do_jump(ret_lab); // if b != 0 goto ret
  vasm.rd_local_int(0); // a
  vasm.rd_local_int(1); // b
  vasm.insn(INSN_MOD_II);
  // stack: [TOP] t=a%b b a
  vasm.rd_local_int(1); // b
  // stack: [TOP] b t=a%b b a
  vasm.wr_local_int(0);
  // stack: [TOP] t b a
  vasm.wr_local_int(1);
  vasm.do_jump(start_lab); // goto start
  vasm.do_label(ret_lab); // label ret
  vasm.insn(INSN_POP_I);
  vasm.insn(INSN_PRINT_I);
  vasm.insn(INSN_RET);
  vasm.end_func();
#else
  vm_asm vasm;
  vasm.begin_func();
  loc_atom lab1 = vasm.make_loc();
  loc_atom lab2 = vasm.make_loc();  
  vasm.const_real(2.0f);
  vasm.do_jump(lab1);
  vasm.const_real(2.0f);
  vasm.do_label(lab1);
  vasm.const_real(2.0f);
  vasm.insn(INSN_ADD_FF);
  vasm.insn(INSN_PRINT_F);
  vasm.insn(INSN_RET); 
  vasm.end_func();
#endif

  script_state *st = vasm.finish();
  if(st == NULL) {
    printf("Error assembling: %s\n", vasm.get_error());
    return 1;
  }
  int32_t stack[128];
  stack[127] = 0;
  stack[126] = 1071;
  stack[125] = 462;
  st->frame = st->stack_top = stack+124;
  st->ip = 1;
  step_script(st, 100);
  delete[] st->bytecode; delete[] st->globals;
  delete st;
}
