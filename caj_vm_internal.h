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
#ifndef CAJ_VM_INTERNAL_H
#define CAJ_VM_INTERNAL_H

static int caj_vm_check_types(uint8_t stype, uint8_t vtype) {
  return stype != vtype && ((stype != VM_TYPE_INT && stype != VM_TYPE_FLOAT) ||
			    (vtype != VM_TYPE_INT && vtype != VM_TYPE_FLOAT));
}

class asm_verify {
 private:
  const char * &err;
  
 public:
  std::vector<uint8_t> stack_types; // FIXME - make private?

   asm_verify(const char* &err_out) : err(err_out) {
   
  }
  
 asm_verify(const char* &err_out, vm_function *func) : err(err_out) {
    // the caller has to allocate space for the return value, to avoid 
    // much messing around in the runtime!
    push_val(func->ret_type);
    push_val(VM_TYPE_OUR_RET_ADDR);
    for(int i = 0; i < func->arg_count; i++) {
      push_val(func->arg_types[i]);
    }
  }

  asm_verify* dup(void) {
    asm_verify *verify = new asm_verify(err);
    verify->stack_types = stack_types;
    return verify;
  }

  void dump_stack(const char*prefix) { // DEBUG
    printf("%sstack: ", prefix);
    for(unsigned i = 0; i < stack_types.size(); i++) {
      printf("%i ", stack_types[i]);
    }
    printf("\n");
  }

  void combine_verify(asm_verify *v2) {
    if(err != NULL) return;
    if(stack_types.size() != v2->stack_types.size()) {
      dump_stack("First "); v2->dump_stack("Second ");
      err = "Stack mismatch"; return;
    }
    for(unsigned i = 0; i < stack_types.size(); i++) {
      if(caj_vm_check_types(stack_types[i], v2->stack_types[i])) {
	dump_stack("First "); v2->dump_stack("Second ");
	err = "Stack mismatch"; return;
      }
    }
  }

  void pop_val(uint8_t vtype) {
    if(err != NULL || vtype == VM_TYPE_NONE) return;
    if(stack_types.empty()) {
      err = "Pop on empty stack"; return;
    }
    switch(vtype) {
    case VM_TYPE_VECT: 
      for(int i = 0; i < 3; i++) pop_val(VM_TYPE_FLOAT);
      return;
    case VM_TYPE_ROT: 
      for(int i = 0; i < 4; i++) pop_val(VM_TYPE_FLOAT);
      return;
    }
    uint8_t stype = stack_types.back();
    stack_types.pop_back();
    if(caj_vm_check_types(stype, vtype)) {
      err = "Type mismatch on stack"; return;
    }
  }

  void push_val(uint8_t vtype) {
    if(err != NULL || vtype == VM_TYPE_NONE) return;
    switch(vtype) {
    case VM_TYPE_VECT: 
      for(int i = 0; i < 3; i++) push_val(VM_TYPE_FLOAT);
      return;
    case VM_TYPE_ROT: 
      for(int i = 0; i < 4; i++) push_val(VM_TYPE_FLOAT);
      return;
    }
    stack_types.push_back(vtype);
  }

 private:
  int get_local(int offset, uint8_t &vtype) {
    int soff = stack_types.size() - 1, fudge = 0;
    for( ; soff >= 0 && offset > 0 ; offset--, soff--) {
      if(stack_types[soff] == VM_TYPE_LIST || 
	 stack_types[soff] == VM_TYPE_STR ||
	 stack_types[soff] == VM_TYPE_KEY) {
	fudge++;
      }
    }
    if(soff < 0) { err = "Local access goes off stack end"; return 0; }
    vtype = stack_types[soff]; return fudge;
  }

 public:
  int check_rdl_i(int offset) {
    if(offset <= 0) { err = "RDL_I with bogus offset"; return 0; }
    uint8_t vtype; 
    int fudge = get_local(offset-1, vtype);
    if(err != NULL) return 0;
    if(caj_vm_check_types(vtype, VM_TYPE_INT)) { err = "RDL_I from wrong type"; return 0; }
    push_val(vtype);
    return fudge;
  }

  int check_wrl_i(int offset) {
    if(offset <= 0) { err = "WRL_I with bogus offset"; return 0; }
    uint8_t vtype; 
    int fudge = get_local(offset, vtype);
    if(err != NULL) return 0;
    if(caj_vm_check_types(vtype, VM_TYPE_INT)) { err = "WRL_I from wrong type"; return 0; }
    pop_val(vtype); // should really be before the check...
    return fudge;
  }
};


// FIXME - move this somewhere saner
class vm_serialiser {
 private:
  std::vector<vm_heap_entry> heap;
  std::vector<vm_function*> funcs;
  unsigned char* data; int data_len, data_alloc;
  uint16_t* bytecode; uint32_t bytecode_len;
  int32_t *gvals; uint16_t gvals_len;
  uint32_t *gptrs; uint16_t gptrs_len;

  void make_space(int len) {
    if(data_len+len > data_alloc) {
      while(data_len+len > data_alloc) data_alloc *= 2;
      data = (unsigned char*)realloc(data, data_alloc);
      if(data == NULL || data_len > data_alloc) abort();
    }
  }

  void write_data(unsigned char* dat, size_t len) {
    make_space(len);
    memcpy(data+data_len, dat, len);
    data_len += len;
  }

  void write_u8(uint8_t val) {
    make_space(1);
    data[data_len++] = val;
  }
  
  void write_u16(uint32_t val) {
    make_space(2);
    data[data_len++] = (val >> 8) & 0xff;
    data[data_len++] = (val) & 0xff;
  }

  void write_u32(uint32_t val) {
    make_space(4);
    data[data_len++] = (val >> 24) & 0xff;
    data[data_len++] = (val >> 16) & 0xff;
    data[data_len++] = (val >> 8) & 0xff;
    data[data_len++] = (val) & 0xff;
  }

 public:
  vm_serialiser() : data(NULL), bytecode(NULL), gvals(NULL), gptrs(NULL) {
     
  }

  uint32_t add_heap_entry(uint8_t vtype, uint32_t len, void* data) {
    vm_heap_entry entry; uint32_t addr;
    entry.vtype = vtype; entry.len = len; 
    entry.data = (unsigned char*)data;
    addr = heap.size();
    heap.push_back(entry);
    return addr;
  }

  void set_bytecode(uint16_t* code, uint32_t len) {
    bytecode = code; bytecode_len = len;
  }
  
  void set_gvals(int32_t* vals, uint16_t len) {
    gvals = vals; gvals_len = len;
  }

  void set_gptrs(uint32_t* vals, uint16_t len) {
    gptrs = vals; gptrs_len = len;
  }

  void add_func(vm_function *func) {
    funcs.push_back(func);
  }

  unsigned char* serialise(size_t *len) {
    free(data); data = NULL;
    assert(gvals != NULL); assert(gptrs != NULL); assert(bytecode != NULL);

    data_len = 0; data_alloc = 256;
    data = (unsigned char*)malloc(data_alloc);
    write_u32(0xf0b17ecd);

    // write heap
    write_u32(heap.size());
    for( std::vector<vm_heap_entry>::iterator heap_iter = heap.begin();
	 heap_iter != heap.end(); heap_iter++) {
      write_u8(heap_iter->vtype);
      write_u32(heap_iter->len);
      switch(heap_iter->vtype) {
      case VM_TYPE_STR:
      default: // FIXME - handle lists, etc
	write_data(heap_iter->data, heap_iter->len);
      }
    }
    
    // write globals
    write_u16(gvals_len);
    for(unsigned int i = 0; i < gvals_len; i++) {
      write_u32((uint32_t)gvals[i]);
    }
    write_u16(gptrs_len);
    for(unsigned int i = 0; i < gptrs_len; i++) {
      write_u32(gptrs[i]);
    }

    write_u16(funcs.size());
    for(unsigned int i = 0; i < funcs.size(); i++) {
      write_u8(funcs[i]->ret_type);
      write_u8(funcs[i]->arg_count);
      for(int j = 0; j < funcs[i]->arg_count; j++) {
	write_u8(funcs[i]->arg_types[j]);
      }
      int slen = strlen(funcs[i]->name);
      write_u8(slen);
      write_data((unsigned char*)funcs[i]->name, slen);
      write_u32(funcs[i]->insn_ptr);
    }

    // write bytecode
    write_u32(bytecode_len);
    for(unsigned int i = 0; i < bytecode_len; i++) {
      write_u16(bytecode[i]);
    }
    
    *len = data_len;
    return data;
  }
};

#endif
