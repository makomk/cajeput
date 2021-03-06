/* Copyright (c) 2009-2010 Aidan Thornton, all rights reserved.
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
#include <stdlib.h>
#include "caj_vm_internal.h"

class loc_atom {
  friend class vm_asm;
private:
  int val;
  loc_atom(int num) : val(num) { };
public:
  loc_atom() : val(-1) { };
  loc_atom(const loc_atom &src) : val(src.val) { };
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
  long empty_list_entry; // FIXME - HACK

  std::vector<uint16_t> bytecode;

  // global variables
  std::vector<int32_t> gvals; // int/float
  std::vector<uint8_t> gval_types;
  std::vector<uint32_t> gptrs; // pointer
  std::vector<uint8_t> gptr_types;

  std::map<int32_t,uint16_t> consts;
  std::map<std::string,uint16_t> const_strs;
  std::vector<uint32_t> loc_map;
  std::vector<asm_verify*> loc_verify;
  std::vector<jump_fixup> fixups;
  std::vector<vm_function*> funcs;

  std::vector<uint32_t> list_build;

  vm_serialiser serial;


private:
  void do_fixup(const jump_fixup &fixup) {
    // bytecode verification checks are done at an earlier stage (via the 
    // locations code) so we don't have to do them here.
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
  vm_asm() : func_start(0), err(NULL), verify(NULL), cond_flag(0),
    empty_list_entry(-1){
    bytecode.push_back(INSN_QUIT);
  }

  ~vm_asm() { // FIXME - incomplete
    for(std::vector<asm_verify*>::iterator iter = loc_verify.begin();
	iter != loc_verify.end(); iter++) {
      delete *iter;
    }
    delete verify;
  }

  int vtype_size(uint8_t vtype) {
    switch(vtype) {
    case VM_TYPE_NONE:
      return 0; // for return values, mainly
    case VM_TYPE_INT:
    case VM_TYPE_FLOAT:
    case VM_TYPE_STR:
    case VM_TYPE_KEY:
    case VM_TYPE_LIST:
      return 1;
    case VM_TYPE_VECT:
      return 3;
    case VM_TYPE_ROT:
      return 4;
    default: printf("ERROR: bad vtype in vtype_size()\n"); abort();
    }   
  }

  // note - it's up to you to make sure the argument types and function name
  // don't get freed before we're done with them.
  const vm_function *add_func(uint8_t ret_type, uint8_t *arg_types, int arg_count,
			      char *name) {
    uint16_t frame_sz = 0; // WARNING - calculated differently than in finish()
    vm_function *func = new vm_function();
    assert(arg_count < 255); // FIXME !

    uint16_t *arg_offsets = new uint16_t[arg_count];
    func->name = name;
    func->ret_type = ret_type;
    frame_sz += vtype_size(ret_type)+1; // for the return address
    func->arg_types = arg_types;
    for(int i = 0; i < arg_count; i++) {
      arg_offsets[i] = frame_sz;
      frame_sz += vtype_size(arg_types[i]);
    }
    func->arg_offsets = arg_offsets; // FIXME - need to free this in cleanup!
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

    /* printf("DEBUG: assembler: starting function %s (number %i) @ %i\n", 
       cfunc->name, cfunc->func_num, (int)bytecode.size()); */

    vm_function *func = funcs[cfunc->func_num];
    func->insn_ptr = func_start = bytecode.size();
    verify = new asm_verify(err, func, 1);
    
    
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
      verify->combine_verify(loc_verify[loc.val]);
    }
  }

  void do_jump(loc_atom loc) {
    if(err != NULL) return;
    if(func_start == 0) { err = "do_jump outside of func"; return; } 
    if(verify == NULL) { err = "Unverifiable code ordering"; return; }
    if(loc.val < 0 || loc.val >= loc_map.size()) { 
      err = "do_label with bad atom"; return; 
    }
    jump_fixup fixup;
    fixup.dest_loc = loc.val; fixup.insn_pos = bytecode.size();
    bytecode.push_back(INSN_QUIT); // replaced later
    if(loc_map[fixup.dest_loc] != 0) {
      do_fixup(fixup);
    } else {
      fixups.push_back(fixup);
    }
    if(loc_verify[fixup.dest_loc] != NULL) {
      verify->combine_verify(loc_verify[fixup.dest_loc]);
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

  // special loc_atom, for use with verify_stack only
  loc_atom mark_stack(void) {
    if(err != NULL) return loc_atom(-1);
    if(func_start == 0) { err = "mark_stack outside of func"; return loc_atom(-1); } 
    if(verify == NULL) { err = "mark_stack at unverifiable point"; return loc_atom(-1); } 
    int pos = loc_map.size();
    loc_map.push_back(0xffffffff);
    loc_verify.push_back(verify->dup());
    return loc_atom(pos);
  }

  void verify_stack(loc_atom mark) {
    if(err != NULL) return;
    if(func_start == 0) { err = "verify_stack outside of func"; return; } 
    if(mark.val < 0 || mark.val >= loc_map.size() || 
       loc_map[mark.val] != 0xffffffff) { 
      err = "verify_stack with bad atom"; return; 
    }
    if(verify == NULL) {
      // use this as a hint to the stack's state. We'll verify this later.
      verify = loc_verify[mark.val]->dup();
    } else {
      verify->combine_verify(loc_verify[mark.val]);
    }
  }

  void dump_stack(const char*prefix) {
    if(err != NULL) return;
    assert(verify != NULL);
    verify->dump_stack(prefix);
  }

  void begin_call(const vm_function *func) {
    if(err != NULL) return;
    if(func_start == 0) { err = "Call outside of func"; return; }
    if(cond_flag) { err = "Non-jump instruction after cond"; return; }
    if(verify == NULL) { err = "Unverifiable code ordering"; return; }    

    // again, if this fails, someone's done something *really* stupid
    assert(func->func_num < funcs.size());
    assert(funcs[func->func_num] == func);

    switch(func->ret_type) {
    case VM_TYPE_NONE: 
      break;
    case VM_TYPE_INT:
    case VM_TYPE_FLOAT:
      const_int(0); break;
    case VM_TYPE_STR:
    case VM_TYPE_KEY:
      const_str(""); break;
    case VM_TYPE_LIST:
      empty_list(); break;
    case VM_TYPE_VECT:
      const_int(0); const_int(0); const_int(0);
      break;
    case VM_TYPE_ROT:
      const_int(0); const_int(0); const_int(0); const_int(0);
      break;
    default:
      err = "Unhandled func return type"; return;
    }

    insn(INSN_BEGIN_CALL);
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
      verify->pop_val(func->arg_types[i]); 
    }
    verify->pop_val(VM_TYPE_RET_ADDR);

    // FIXME - should really verify return value somehow!

    if(err != NULL) return;
    bytecode.push_back(MAKE_INSN(ICLASS_CALL, func->func_num));
  }

  // FIXME - add_global_val/add_global_ptr should be private!
  uint16_t add_global_val(int32_t val, uint8_t vtype) {
    uint16_t ret = gvals.size();
    gvals.push_back(val);
    gval_types.push_back(vtype);
    return ret;
  }

  uint16_t add_global_ptr(int32_t val, uint8_t vtype) {
    uint16_t ret = gptrs.size();
    gptrs.push_back(val);
    gptr_types.push_back(vtype);
    return ret;
  }

  uint16_t add_global_int(int32_t val) {
    return add_global_val(val, VM_TYPE_INT);
  }

  uint16_t add_global_float(float val) {
    union { float f; int32_t i; } u;
    u.f = val;
    return add_global_val(u.i, VM_TYPE_FLOAT);
  }

  uint16_t add_const(int32_t val) {
    std::map<int32_t,uint16_t>::iterator iter = consts.find(val);
    if(iter == consts.end()) {
      uint16_t ret = add_global_val(val, VM_TYPE_INT);
      consts[val] = ret; return ret;
    } else {
      return iter->second;
    }
  }

  uint32_t add_string(const char* val) {
    // FIXME - reuse strings
    int len = strlen(val);
    char *dat = strdup(val);
    return serial.add_heap_entry(VM_TYPE_STR,len,dat);
  }

  uint16_t add_const_str(const char *val) {
    std::map<std::string,uint16_t>::iterator iter = const_strs.find(val);
    if(iter == const_strs.end()) {
      uint16_t ret = add_global_ptr(add_string(val), VM_TYPE_STR);
      const_strs[val] = ret; return ret;
    } else {
      return iter->second;
    }
  }

  void begin_list() {
    assert(list_build.size() == 0);
  }

  void list_add_str(const char* val) {
    list_build.push_back(add_string(val));
  }

  void list_add_int(int32_t val) {
    unsigned char *data = (unsigned char*)malloc(4);
    serial.int_to_bin(val, data);
    list_build.push_back(serial.add_heap_entry(VM_TYPE_INT,4,data));
  }

  void list_add_float(float val) {
    unsigned char *data = (unsigned char*)malloc(4);
    serial.float_to_bin(val, data);
    list_build.push_back(serial.add_heap_entry(VM_TYPE_FLOAT,4,data));
  }


  void list_add_vect(float *val) {
    unsigned char *data = (unsigned char*)malloc(12);
    // FIXME - ordering?
    serial.float_to_bin(val[0], data+0);
    serial.float_to_bin(val[1], data+4);
    serial.float_to_bin(val[2], data+8);
    list_build.push_back(serial.add_heap_entry(VM_TYPE_VECT,12,data));
     
  }

  void list_add_rot(float *val) {
    unsigned char *data = (unsigned char*)malloc(16);
    // FIXME - ordering?
    serial.float_to_bin(val[0], data+0);
    serial.float_to_bin(val[1], data+4);
    serial.float_to_bin(val[2], data+8);
    serial.float_to_bin(val[3], data+12);
    list_build.push_back(serial.add_heap_entry(VM_TYPE_ROT,16,data));
  }

  uint32_t end_list() {
    int count = list_build.size();
    if(count >  VM_LIMIT_HEAP || count*4 >  VM_LIMIT_HEAP) {
      err = "List too long"; return 0;
    }
    unsigned char *data = (unsigned char*)malloc(count*4);
    unsigned offset = 0;
    for(int i = 0; i < count; i++) {
      uint32_t val = list_build[i];
      data[offset++] = (val >> 24) & 0xff;
      data[offset++] = (val >> 16) & 0xff;
      data[offset++] = (val >> 8) & 0xff;
      data[offset++] = (val) & 0xff;
    }
    list_build.clear();
    return serial.add_heap_entry(VM_TYPE_LIST,count*4,data);
  }

  void insn(uint16_t val) {
    if(err != NULL) return;
    if(func_start == 0) { err = "Instruction outside of func"; return; }
    if(cond_flag) { err = "Non-jump instruction after cond"; return; }
    if(verify == NULL) { err = "Unverifiable code ordering"; return; }
    switch(GET_ICLASS(val)) {
    case ICLASS_NORMAL:
      {
	uint16_t ival = GET_IVAL(val);
	if(ival >= NUM_INSNS) { err = "Invalid instruction"; return; }
	insn_info info = vm_insns[ival];
	verify->pop_val(info.arg2); verify->pop_val(info.arg1);
	verify->push_val(info.ret);
	
	if(err != NULL) return;

	switch(info.special) {
	case IVERIFY_INVALID:
	  err = "Invalid instruction"; return;
	case IVERIFY_NORMAL: 
	  break;
	case IVERIFY_COND:
	  cond_flag = 1; break;
	case IVERIFY_RET:
	  verify->pop_val(VM_TYPE_OUR_RET_ADDR);
	  delete verify; verify = NULL;
	  break; 
	}
      }
      break;
    case ICLASS_RDG_I:
      {
	uint16_t ival = GET_IVAL(val); 
	if(ival >= gval_types.size() || 
	   caj_vm_check_types(gval_types[ival], VM_TYPE_INT)) {// FIXME - type check redundant
	   err = "Bad global variable read"; return;
	}
	verify->push_val(VM_TYPE_INT);
	break;
      }
    case ICLASS_WRG_I:
      {
	uint16_t ival = GET_IVAL(val);
	if(ival >= gval_types.size() || 
	   caj_vm_check_types(gval_types[ival], VM_TYPE_INT)) {// FIXME - type check redundant
	   err = "Bad global variable write"; return;
	}
	verify->pop_val(VM_TYPE_INT);
	break;
      }
    case ICLASS_RDG_P:
      {
	uint16_t ival = GET_IVAL(val);
	if(ival >= gptr_types.size() || 
	   (gptr_types[ival] != VM_TYPE_STR &&
	    gptr_types[ival] != VM_TYPE_KEY &&
	    gptr_types[ival] != VM_TYPE_LIST)) {
	   err = "Bad global pointer read"; return;
	}
	verify->push_val(gptr_types[ival]);
	break;
      }
    case ICLASS_WRG_P:
      {
	uint16_t ival = GET_IVAL(val);
	if(ival >= gptr_types.size() || 
	   (gptr_types[ival] != VM_TYPE_STR &&
	    gptr_types[ival] != VM_TYPE_KEY &&
	    gptr_types[ival] != VM_TYPE_LIST)) {
	   err = "Bad global pointer write"; return;
	}
	verify->pop_val(gptr_types[ival]);
	break;
      }
    case ICLASS_RDL_I:
      // not verified fully - use rd_local_int wrapper (FIXME)
      verify->push_val(VM_TYPE_INT);
      break;
    case ICLASS_WRL_I:
      // not verified fully - use wr_local_int wrapper (FIXME)
      verify->pop_val(VM_TYPE_INT);
      break;
    case ICLASS_RDL_P:
      {
	uint16_t ival = GET_IVAL(val);
	verify->check_rdl_p(ival);
	break;
      }
    case ICLASS_WRL_P:
      {
	uint16_t ival = GET_IVAL(val);
	verify->check_wrl_p(ival);
	break;
      }
      break;
    default:
      err = "Unknown instruction class"; return;
    }
    bytecode.push_back(val);
  }

  void rd_local_int(unsigned offset) {
    if(err != NULL) return;
    if(verify == NULL) { err = "Unverifiable code ordering"; return; }
    if(offset >= verify->stack_types.size()) {
      err = "Local variable out of bounds"; return;
    }
    if(caj_vm_check_types(verify->stack_types[offset], VM_TYPE_INT)) {
      err = "Local variable of wrong type"; return;
    }
    offset = verify->stack_types.size()-offset;
    insn(MAKE_INSN(ICLASS_RDL_I, offset));
  }

  void wr_local_int(unsigned offset) {
    if(err != NULL) return;
    if(verify == NULL) { err = "Unverifiable code ordering"; return; }
    if(offset >= verify->stack_types.size()-1) {
      err = "Local variable out of bounds"; return;
    }
    if(caj_vm_check_types(verify->stack_types[offset], VM_TYPE_INT)) {
      err = "Local variable of wrong type"; return;
    }
    offset = verify->stack_types.size()-1-offset;
    insn(MAKE_INSN(ICLASS_WRL_I, offset));
  }

  void rd_local_ptr(unsigned offset) {

    if(err != NULL) return;
    if(verify == NULL) { err = "Unverifiable code ordering"; return; }
    if(offset >= verify->stack_types.size()) {
      err = "Local variable out of bounds"; return;
    }
    if(verify->stack_types[offset] != VM_TYPE_STR &&
       verify->stack_types[offset] != VM_TYPE_KEY &&
       verify->stack_types[offset] != VM_TYPE_LIST) {
      err = "Local variable of wrong type"; return;
    }
    offset = verify->stack_types.size()-offset;
    insn(MAKE_INSN(ICLASS_RDL_P, offset)); // FIXME - not right yet!

  }

  void wr_local_ptr(unsigned offset) {
    if(err != NULL) return;
    if(verify == NULL) { err = "Unverifiable code ordering"; return; }
    if(offset >= verify->stack_types.size()-1) {
      err = "Local variable out of bounds"; return;
    }
    if(verify->stack_types[offset] != VM_TYPE_STR && 
       verify->stack_types[offset] != VM_TYPE_KEY && 
       verify->stack_types[offset] != VM_TYPE_LIST) {
      err = "Local variable of wrong type"; return;
    }
    offset = verify->stack_types.size()-1-offset;
    insn(MAKE_INSN(ICLASS_WRL_P, offset));
  }

  uint16_t const_int(int32_t val) {
    insn(MAKE_INSN(ICLASS_RDG_I, add_const(val)));
    return verify == NULL ? 0 : verify->stack_types.size() - 1;
  }

  uint16_t const_real(float val) {
    union { float f; int32_t i; } u;
    u.f = val;
    insn(MAKE_INSN(ICLASS_RDG_I, add_const(u.i)));
    return verify == NULL ? 0 : verify->stack_types.size() - 1;
  }

  uint16_t const_str(const char* val) {
    insn(MAKE_INSN(ICLASS_RDG_P, add_const_str(val)));
    return verify == NULL ? 0 : verify->stack_types.size() - 1;
  }

  uint32_t get_empty_list(void) {
    // FIXME - HACK
    if(empty_list_entry < 0) {
      begin_list();
      empty_list_entry = add_global_ptr(end_list(), VM_TYPE_LIST);
    }
    return empty_list_entry;
  }

  uint16_t empty_list(void) {
    
    insn(MAKE_INSN(ICLASS_RDG_P, get_empty_list()));
    return verify == NULL ? 0 : verify->stack_types.size() - 1;
  }


  void clear_stack(void) {
    if(err != NULL) return;
    if(func_start == 0) { err = "clear_stack outside of func"; return; }
    if(verify == NULL) { err = "Unverifiable code ordering"; return; }
    while(!verify->stack_types.empty()) {
      switch(verify->stack_types.back()) {
      case VM_TYPE_INT:
      case VM_TYPE_FLOAT:
	insn(INSN_DROP_I); break;
      case VM_TYPE_STR:
      case VM_TYPE_KEY:
      case VM_TYPE_LIST:
	insn(INSN_DROP_P); break;
      case VM_TYPE_OUR_RET_ADDR:
	return; // we're done!
      default:
	err = "Unhandled type in clear_stack"; return;
      }
      if(err != NULL) return;
    }
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

  unsigned char* finish(size_t *len) {
    if(err != NULL) return NULL;
    if(func_start != 0) { 
      err = "Need to end function before finishing";
      return NULL;
    }
   
    uint16_t *s_bytecode = new uint16_t[bytecode.size()];
    for(unsigned i = 0; i < bytecode.size(); i++)
      s_bytecode[i] = bytecode[i];
    serial.set_bytecode(s_bytecode, bytecode.size());

    int32_t *s_gvals = new int32_t[gvals.size()];
    for(unsigned i = 0; i < gvals.size(); i++)
      s_gvals[i] = gvals[i];
    serial.set_gvals(s_gvals, gvals.size());

    uint32_t *s_gptrs = new uint32_t[gptrs.size()];
    for(unsigned i = 0; i < gptrs.size(); i++)
      s_gptrs[i] = gptrs[i];
    serial.set_gptrs(s_gptrs, gptrs.size());

    for(unsigned i = 0; i < funcs.size(); i++) {
      serial.add_func(funcs[i]);
    }
    

    unsigned char* ret = serial.serialise(len);
    delete[] s_bytecode; delete[] s_gvals; delete[] s_gptrs;
    return ret;
  }
  
  const char* get_error(void) {
    return err;
  }
};

#endif
