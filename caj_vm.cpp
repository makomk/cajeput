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

#define ICLASS_NORMAL 0
#define ICLASS_JUMP   1
#define ICLASS_RDG_I  2
#define ICLASS_WRG_I  3
#define ICLASS_RDG_P  4
// #define ICLASS_WRG_P  5
// #define ICLASS_RDG_V  6
// #define ICLASS_WRG_V  7
#define ICLASS_RDL_I 8
#define ICLASS_WRL_I 9

#define GET_ICLASS(insn) (((insn) >> 12) &0xf)
#define GET_IVAL(insn) ((insn) & 0xfff)

#define MAKE_INSN(iclass,ival) ((iclass) << 12 | (ival))

#define unlikely(x)     __builtin_expect((x),0)

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <map>
#include <string>
#include <vector>

// todo - unitary negation!
#define INSN_NOOP   0
#define INSN_ABORT  1 // should never appear in actual bytecode!
#define INSN_ADD_II 2
#define INSN_SUB_II 3
#define INSN_MUL_II 4
#define INSN_DIV_II 5
#define INSN_ADD_FF 6
#define INSN_SUB_FF 7
#define INSN_MUL_FF 8
#define INSN_DIV_FF 9
#define INSN_RET    10
#define INSN_MOD_II 11
#define INSN_AND_II 12 // & - todo
#define INSN_OR_II  13 // | - todo
#define INSN_XOR_II 14 // ^ - todo
#define INSN_NOT_II 15 // ~ - todo
#define INSN_SHL    16 // << - todo
#define INSN_SHR    17 // >> - todo
#define INSN_AND_L  18 // && - non-short circuiting, of course
#define INSN_OR_L   19 // ||
#define INSN_NOT_L  20 // !
#define INSN_COND   21 // pops value from stack, skips next insn if it's 0
#define INSN_NCOND  22 // pops value from stack, skips next insn unless it's 0
#define INSN_EQ_II  23 // ==
#define INSN_NEQ_II 24 // !=
#define INSN_GR_II  25 // >
#define INSN_LES_II 26 // <
#define INSN_GEQ_II 27 // >=
#define INSN_LEQ_II 28 // <=
#define INSN_POP_I 29 
// #define INSN_POP_P 30
// #define INSN_POP_I3 31 // POP_I*3
// #define INSN_POP_I4 32 // POP_I*4
#define INSN_QUIT 0xff0
#define INSN_PRINT_I 0xff1
#define INSN_PRINT_F 0xff2
#define INSN_PRINT_STR 0xff3

#define VM_TYPE_NONE  0
#define VM_TYPE_INT   1
#define VM_TYPE_FLOAT 2
#define VM_TYPE_STR   3
#define VM_TYPE_KEY   4
#define VM_TYPE_VECT  5
#define VM_TYPE_ROT   6

struct script_state {
  uint32_t ip;
  uint16_t* bytecode;
  int32_t* stack_top;
  int32_t* frame;
  int32_t* globals;
  uint32_t* heap;
  int scram_flag;
};

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

class loc_atom {
  friend class vm_asm;
private:
  int val;
  loc_atom(int num) : val(num) { };
public:
  loc_atom() : val(-1) { };
  loc_atom(const loc_atom &src) : val(src.val) { };
};

class vm_asm {
private:
  struct jump_fixup {
    int dest_loc;
    uint32_t insn_pos;
  };

  uint32_t func_start;
  const char* err;
  std::vector<uint16_t> bytecode;
  std::vector<int32_t> globals;
  std::map<int32_t,uint16_t> consts;
  std::vector<uint32_t> loc_map;
  std::vector<jump_fixup> fixups;

private:
  void do_fixup(const jump_fixup &fixup) {
    if(loc_map[fixup.dest_loc] == 0) {
      err = "Jump without matching label"; return;
    }
    int32_t offset = ((int32_t)loc_map[fixup.dest_loc])-(int32_t)(fixup.insn_pos+1);
    if(offset < -2047 || offset > 2047) {
      err = "Jump too big"; return; // FIXME
    }
    if(offset < 0) {
      bytecode[fixup.insn_pos] = MAKE_INSN(ICLASS_JUMP, 0x800|(-offset));
    } else {
      bytecode[fixup.insn_pos] = MAKE_INSN(ICLASS_JUMP, offset);
    }
  }

public:
  vm_asm() : func_start(0), err(NULL) {
    bytecode.push_back(INSN_QUIT);
  }

  void begin_func(void) {
    if(err != NULL) return;
    if(func_start != 0) { err = "func_start in func"; return; }
    func_start = bytecode.size();
  }

  loc_atom make_loc(void) {
    if(err != NULL) return loc_atom(-1);
    if(func_start == 0) { 
      err = "make_atom outside of func"; return loc_atom(-1);
    }
    int pos = loc_map.size();
    loc_map.push_back(0);
    return loc_atom(pos);
  }

  void do_label(loc_atom loc) {
    if(err != NULL) return;
    if(func_start == 0) { err = "do_label outside of func"; return; }
    if(loc.val < 0 || loc.val >= loc_map.size()) { 
      err = "do_label with bad atom"; return; 
    }
    if(loc_map[loc.val] != 0) {
      err = "duplicate do_label for atom"; return; 
    }
    loc_map[loc.val] = bytecode.size();
  }

  void do_jump(loc_atom loc) {
    if(err != NULL) return;
    if(func_start == 0) { err = "do_jump outside of func"; return; }    
    jump_fixup fixup;
    fixup.dest_loc = loc.val; fixup.insn_pos = bytecode.size();
    bytecode.push_back(INSN_QUIT); // replaced later
    if(loc_map[fixup.dest_loc] != 0) {
      do_fixup(fixup);
    } else {
      fixups.push_back(fixup);
    }
  }

  uint16_t add_const(int32_t val) {
    std::map<int32_t,uint16_t>::iterator iter = consts.find(val);
    if(iter == consts.end()) {
      uint16_t ret = globals.size();
      globals.push_back(val);
      consts[val] = ret; return ret;
    } else {
      return iter->second;
    }
  }

  void insn(uint16_t val) {
    if(err != NULL) return;
    if(func_start == 0) { err = "Instruction outside of func"; return; }
    bytecode.push_back(val);
  }

  void const_int(int32_t val) {
    insn(MAKE_INSN(ICLASS_RDG_I, add_const(val)));
  }

  void const_real(float val) {
    union { float f; int32_t i; } u;
    u.f = val;
    insn(MAKE_INSN(ICLASS_RDG_I, add_const(u.i)));
  }

  void end_func(void) {
    // FIXME - could do more verification here
    if(err != NULL) return;
    if(func_start == 0) { err = "end_func outside of func"; return; }

    for(std::vector<jump_fixup>::iterator iter = fixups.begin();
	iter != fixups.end(); iter++) {
      do_fixup(*iter);
    }
    func_start = 0; 
    fixups.clear(); 
    loc_map.clear(); // FIXME - add atom start offset to detect cross-func jump
    
  }

  script_state* finish(void) {
    if(err != NULL) return NULL;
    if(func_start != 0) { 
      err = "Need to end function before finishing";
      return NULL;
    }
    script_state* st = new script_state();
    st->bytecode = new uint16_t[bytecode.size()];
    for(int i = 0; i < bytecode.size(); i++)
      st->bytecode[i] = bytecode[i];

    st->globals = new int32_t[globals.size()];
    for(int i = 0; i < globals.size(); i++)
      st->globals[i] = globals[i];
    return st;
  }
  
  const char* get_error(void) {
    return err;
  }
};

int main(void) {
  // Test function - calculates the GCD of 1071 and 462

  vm_asm vasm;
  vasm.begin_func();
  loc_atom start_lab = vasm.make_loc();
  loc_atom ret_lab = vasm.make_loc();
  vasm.const_int(1071); // a
  vasm.const_int(462); // b
  vasm.do_label(start_lab); // label start
  // Right now, stack looks like [TOP] b a
  vasm.insn(MAKE_INSN(ICLASS_RDL_I, 1)); // b
  vasm.insn(INSN_NCOND);
  vasm.do_jump(ret_lab); // if b != 0 goto ret
  vasm.insn(MAKE_INSN(ICLASS_RDL_I, 2)); // a
  vasm.insn(MAKE_INSN(ICLASS_RDL_I, 2)); // b
  vasm.insn(INSN_MOD_II);
  // stack: [TOP] t=a%b b a
  vasm.insn(MAKE_INSN(ICLASS_RDL_I, 2)); // b
  // stack: [TOP] b t=a%b b a
  vasm.insn(MAKE_INSN(ICLASS_WRL_I, 3)); // a = b
  // stack: [TOP] t b a
  vasm.insn(MAKE_INSN(ICLASS_WRL_I, 1)); // b = t
  vasm.do_jump(start_lab); // goto start
  vasm.do_label(ret_lab); // label ret
  vasm.insn(INSN_POP_I);
  vasm.insn(INSN_PRINT_I);
  vasm.insn(INSN_RET);
  vasm.end_func();

  script_state *st = vasm.finish();
  if(st == NULL) {
    printf("Error assembling: %s\n", vasm.get_error());
    return 1;
  }
  int32_t stack[128];
  stack[127] = 0;
  st->frame = st->stack_top = stack+126;
  st->ip = 1;
  step_script(st, 100);
}
