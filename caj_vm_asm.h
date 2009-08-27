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

#ifndef CAJ_VM_ASM_H
#define CAJ_VM_ASM_H

#include <cassert>
#include <vector>
#include <map>

class loc_atom {
  friend class vm_asm;
private:
  int val;
  loc_atom(int num) : val(num) { };
public:
  loc_atom() : val(-1) { };
  loc_atom(const loc_atom &src) : val(src.val) { };
};

struct asm_verify {
  std::vector<uint8_t> stack_types;

  asm_verify* dup(void) {
    asm_verify *verify = new asm_verify();
    verify->stack_types = stack_types;
    return verify;
  }
};

struct vm_function {
  const char* name;
  uint8_t ret_type;
  uint16_t func_num;
  uint32_t insn_ptr; 
  int arg_count;
  const uint8_t* arg_types;
};

// 
// BIG FAT WARNING!
// Due to the way the code verification is done, the first call to any code
// section must come from BEFORE the code section in question
//
// OK:
//   label foo:
//      ...
//   if something goto baz
//      ...
//   label baz;
//      ...
//   goto foo;
//
// NOT OK:
//    goto foo
//  label baz
//    <some code>
//  label foo:
//    goto baz

class vm_asm {
private:
  struct jump_fixup {
    int dest_loc;
    uint32_t insn_pos;
  };

  uint32_t func_start;
  const char* err;
  asm_verify* verify;
  int cond_flag;

  std::vector<uint16_t> bytecode;
  std::vector<int32_t> globals;
  std::vector<uint8_t> global_types;
  std::map<int32_t,uint16_t> consts;
  std::vector<uint32_t> loc_map;
  std::vector<asm_verify*> loc_verify;
  std::vector<jump_fixup> fixups;
  std::vector<vm_function*> funcs;

  int check_types(uint8_t stype, uint8_t vtype) {
    return stype != vtype && ((stype != VM_TYPE_INT && stype != VM_TYPE_FLOAT) ||
			      (vtype != VM_TYPE_INT && vtype != VM_TYPE_FLOAT));
  }

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

  void combine_verify(asm_verify *v1, asm_verify *v2) {
    if(err != NULL) return;
    if(v1->stack_types.size() != v2->stack_types.size()) {
      err = "Stack mismatch"; return;
    }
    for(int i = 0; i < v1->stack_types.size(); i++) {
      if(check_types(v1->stack_types[i], v2->stack_types[i])) {
	err = "Stack mismatch"; return;
      }
    }
  }

  void pop_val(uint8_t vtype) {
    if(err != NULL || vtype == VM_TYPE_NONE) return;
    if(verify->stack_types.empty()) {
      err = "Pop on empty stack"; return;
    }
    uint8_t stype = verify->stack_types.back();
    verify->stack_types.pop_back();
    if(check_types(stype, vtype)) {
      err = "Type mismatch on stack"; return;
    }
  }

  void push_val(uint8_t vtype) {
    if(err != NULL || vtype == VM_TYPE_NONE) return;
    verify->stack_types.push_back(vtype);
  }

public:
  vm_asm() : func_start(0), err(NULL), verify(NULL), cond_flag(0) {
    bytecode.push_back(INSN_QUIT);
  }

  ~vm_asm() {
    for(std::vector<asm_verify*>::iterator iter = loc_verify.begin();
	iter != loc_verify.end(); iter++) {
      delete *iter;
    }
    delete verify;
  }

  // note - it's up to you to make sure the argument types and function name
  // don't get freed before we're done with them.
  const vm_function *add_func(uint8_t ret_type, uint8_t *arg_types, int arg_count,
			const char *name) {
    vm_function *func = new vm_function();
    func->name = name;
    func->ret_type = ret_type; // FIXME - not handled yet
    func->arg_types = arg_types;
    func->func_num = funcs.size();
    func->insn_ptr = 0;
    func->arg_count = arg_count;
    funcs.push_back(func);
    return func;
  }

  void begin_func(const vm_function *cfunc) {
    if(err != NULL) return;
    if(func_start != 0) { err = "func_start in func"; return; }

    // if this fails, someone's done something *really* stupid
    assert(cfunc->func_num < funcs.size());
    assert(funcs[cfunc->func_num] == cfunc);

    vm_function *func = funcs[cfunc->func_num];
    func->insn_ptr = func_start = bytecode.size();
    verify = new asm_verify();
    for(int i = 0; i < func->arg_count; i++) {
      push_val(func->arg_types[i]);
    }
  }

  loc_atom make_loc(void) {
    if(err != NULL) return loc_atom(-1);
    if(func_start == 0) { 
      err = "make_atom outside of func"; return loc_atom(-1);
    }
    int pos = loc_map.size();
    loc_map.push_back(0);
    loc_verify.push_back(NULL);
    return loc_atom(pos);
  }

  void do_label(loc_atom loc) {
    // FIXME - need to handle jumps properly in verification
    if(err != NULL) return;
    if(func_start == 0) { err = "do_label outside of func"; return; }
    if(loc.val < 0 || loc.val >= loc_map.size()) { 
      err = "do_label with bad atom"; return; 
    }
    if(loc_map[loc.val] != 0) {
      err = "duplicate do_label for atom"; return; 
    }
    loc_map[loc.val] = bytecode.size();
    if(verify == NULL) {
      if(loc_verify[loc.val] != NULL) {
	verify = loc_verify[loc.val]->dup();
      } else {
	
	err = "Unverifiable label placement"; return;
      }
    } else if(loc_verify[loc.val] == NULL)  {
      loc_verify[loc.val] = verify->dup();
    } else {
      combine_verify(loc_verify[loc.val], verify);
    }
  }

  void do_jump(loc_atom loc) {
    // FIXME - need to handle jumps properly in verification
    if(err != NULL) return;
    if(func_start == 0) { err = "do_jump outside of func"; return; } 
    if(verify == NULL) { err = "Unverifiable code ordering"; return; }
    jump_fixup fixup;
    fixup.dest_loc = loc.val; fixup.insn_pos = bytecode.size();
    bytecode.push_back(INSN_QUIT); // replaced later
    if(loc_map[fixup.dest_loc] != 0) {
      do_fixup(fixup);
    } else {
      fixups.push_back(fixup);
    }
    if(loc_verify[fixup.dest_loc] != NULL) {
      combine_verify(loc_verify[fixup.dest_loc], verify);
      if(!cond_flag) {
	delete verify; verify = NULL;
      }
    } else if(cond_flag) {
      loc_verify[fixup.dest_loc] = verify->dup();
    } else {
      loc_verify[fixup.dest_loc] = verify;
      verify = NULL;
    }
    cond_flag = 0;
  }

  void do_call(const vm_function *func) {
    if(err != NULL) return;
    if(func_start == 0) { err = "Call outside of func"; return; }
    if(cond_flag) { err = "Non-jump instruction after cond"; return; }
    if(verify == NULL) { err = "Unverifiable code ordering"; return; }    

    // again, if this fails, someone's done something *really* stupid
    assert(func->func_num < funcs.size());
    assert(funcs[func->func_num] == func);

    // arguments are pushed on left-to-right
    for(int i = func->arg_count - 1; i >= 0; i--) {
      pop_val(func->arg_types[i]); 
    }
    pop_val(VM_TYPE_RET_ADDR);

    // FIXME - need to handle return value somehow!

    if(err != NULL) return;
    bytecode.push_back(MAKE_INSN(ICLASS_CALL, func->func_num));
  }

  uint16_t add_global(int32_t val, uint8_t vtype) {
    uint16_t ret = globals.size();
    globals.push_back(val);
    global_types.push_back(vtype);
    return ret;
  }

  uint16_t add_const(int32_t val) {
    std::map<int32_t,uint16_t>::iterator iter = consts.find(val);
    if(iter == consts.end()) {
      uint16_t ret = add_global(val, VM_TYPE_INT);
      consts[val] = ret; return ret;
    } else {
      return iter->second;
    }
  }

  void insn(uint16_t val) {
    if(err != NULL) return;
    if(func_start == 0) { err = "Instruction outside of func"; return; }
    if(cond_flag) { err = "Non-jump instruction after cond"; return; }
    if(verify == NULL) { err = "Unverifiable code ordering"; return; }
    switch(GET_ICLASS(val)) {
    case ICLASS_NORMAL:
      {
	int16_t ival = GET_IVAL(val);
	if(ival >= NUM_INSNS) { err = "Invalid instruction"; return; }
	insn_info info = vm_insns[ival];
	pop_val(info.arg1); pop_val(info.arg2);
	push_val(info.ret);
	
	if(err != NULL) return;

	switch(info.special) {
	case IVERIFY_INVALID:
	  err = "Invalid instruction"; return;
	case IVERIFY_NORMAL: 
	  break;
	case IVERIFY_COND:
	  cond_flag = 1; break;
	case IVERIFY_RET:
	  if(verify->stack_types.size() != 0) {
	    err = "Stack not cleared before RET"; return;
	  }
	  delete verify; verify = NULL;
	  break; 
	}
      }
      break;
    case ICLASS_RDG_I:
      // TODO - verify this!
      push_val(VM_TYPE_INT);
      break;
    case ICLASS_RDL_I:
      // not verified fully - use rd_local_int wrapper
      push_val(VM_TYPE_INT);
      break;
    case ICLASS_WRL_I:
      // not verified fully - use wr_local_int wrapper
      pop_val(VM_TYPE_INT);
      break;
    default:
      err = "Unknown instruction class"; return;
    }
    bytecode.push_back(val);
  }

  void rd_local_int(int offset) {
    if(err != NULL) return;
    if(verify == NULL) { err = "Unverifiable code ordering"; return; }
    if(offset < 0 || offset >= verify->stack_types.size()) {
      err = "Local variable out of bounds"; return;
    }
    if(check_types(verify->stack_types[offset], VM_TYPE_INT)) {
      err = "Local variable of wrong type"; return;
    }
    offset = verify->stack_types.size()-offset;
    insn(MAKE_INSN(ICLASS_RDL_I, offset));
  }

  void wr_local_int(int offset) {
    if(err != NULL) return;
    if(verify == NULL) { err = "Unverifiable code ordering"; return; }
    if(offset < 0 || offset >= verify->stack_types.size()-1) {
      err = "Local variable out of bounds"; return;
    }
    if(check_types(verify->stack_types[offset], VM_TYPE_INT)) {
      err = "Local variable of wrong type"; return;
    }
    offset = verify->stack_types.size()-1-offset;
    insn(MAKE_INSN(ICLASS_WRL_I, offset));
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
    if(verify != NULL) { err = "end_func not at end of code"; return; }

    for(std::vector<jump_fixup>::iterator iter = fixups.begin();
	iter != fixups.end(); iter++) {
      do_fixup(*iter);
    }

    for(std::vector<asm_verify*>::iterator iter = loc_verify.begin();
	iter != loc_verify.end(); iter++) {
      delete *iter;
    }

    func_start = 0; 
    fixups.clear(); 
    loc_map.clear(); // FIXME - add atom start offset to detect cross-func jump
    loc_verify.clear();
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

#endif
