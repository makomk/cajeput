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
			    (vtype != VM_TYPE_INT && vtype != VM_TYPE_FLOAT))
                        && ((stype != VM_TYPE_STR && stype != VM_TYPE_KEY) ||
			    (vtype != VM_TYPE_STR && vtype != VM_TYPE_KEY))
    && (stype != VM_TYPE_PTR || (vtype != VM_TYPE_STR && vtype != VM_TYPE_KEY && vtype != VM_TYPE_LIST))
    && (vtype != VM_TYPE_PTR || (stype != VM_TYPE_STR && stype != VM_TYPE_KEY && stype != VM_TYPE_LIST));
}

class asm_verify {
 private:
  const char * &err;
  int ptr_size, stack_used, max_stack_used;
  
 public:
  std::vector<uint8_t> stack_types; // FIXME - make private?

 asm_verify(const asm_verify &v) : err(v.err), ptr_size(v.ptr_size), 
    stack_used(v.stack_used), max_stack_used(v.max_stack_used),
    stack_types(v.stack_types) {
   
  }
  
 asm_verify(const char* &err_out, vm_function *func, int ptr_size_) : 
  err(err_out),ptr_size(ptr_size_), stack_used(0),  max_stack_used(0) {
    // the caller has to allocate space for the return value, to avoid 
    // much messing around in the runtime!
    push_val(func->ret_type);
    push_val(VM_TYPE_OUR_RET_ADDR);
    for(int i = 0; i < func->arg_count; i++) {
      push_val(func->arg_types[i]);
    }
    stack_used = max_stack_used = 0; // arguments don't count
  }

  asm_verify* dup(void) {
    return new asm_verify(*this);
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
    assert(stack_used == v2->stack_used);
  }

  uint8_t pop_val_raw() {
    if(stack_types.empty()) {
      err = "Pop on empty stack"; return 0;
    }
    uint8_t stype = stack_types.back();
    stack_types.pop_back();
    return stype;
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
    case VM_TYPE_INT:
    case VM_TYPE_FLOAT:
    case VM_TYPE_RET_ADDR:
    case VM_TYPE_OUR_RET_ADDR:
      stack_used--; break;
    case VM_TYPE_STR:
    case VM_TYPE_KEY:
    case VM_TYPE_LIST:
      stack_used -= ptr_size; break;
    default: assert(0); break;
    }

    uint8_t stype = stack_types.back();
    stack_types.pop_back();
    if(caj_vm_check_types(stype, vtype)) {
      printf("DEBUG: wanted %i got %i\n", vtype, stype);
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
    case VM_TYPE_INT:
    case VM_TYPE_FLOAT:
    case VM_TYPE_RET_ADDR:
    case VM_TYPE_OUR_RET_ADDR:
      stack_used++; break;
    case VM_TYPE_STR:
    case VM_TYPE_KEY:
    case VM_TYPE_LIST:
      stack_used += ptr_size; break;
    default: printf("ERROR: Unknown vtype %i?!\n",vtype); assert(0); break;
    }

    if(stack_used > max_stack_used) max_stack_used = stack_used;

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
    uint8_t vtype = VM_TYPE_NONE; 
    int fudge = get_local(offset-1, vtype);
    if(err != NULL) return 0;
    if(caj_vm_check_types(vtype, VM_TYPE_INT)) { err = "RDL_I from wrong type"; return 0; }
    push_val(vtype);
    return fudge;
  }

  int check_wrl_i(int offset) {
    if(offset <= 0) { err = "WRL_I with bogus offset"; return 0; }
    uint8_t vtype = VM_TYPE_NONE; 
    int fudge = get_local(offset, vtype);
    if(err != NULL) return 0;
    if(caj_vm_check_types(vtype, VM_TYPE_INT)) { err = "WRL_I from wrong type"; return 0; }
    pop_val(vtype); // should really be before the check...
    return fudge;
  }

  int check_rdl_p(int offset) {
    if(offset <= 0) { err = "RDL_P with bogus offset"; return 0; }
    uint8_t vtype = VM_TYPE_NONE; 
    int fudge = get_local(offset-1, vtype);
    if(err != NULL) return 0;
    if(vtype != VM_TYPE_STR && vtype != VM_TYPE_KEY && vtype != VM_TYPE_LIST) {
      err = "RDL_P from wrong type"; return 0; 
    }
    push_val(vtype);
    return fudge;
  }

  int check_wrl_p(int offset) {
    if(offset <= 0) { err = "WRL_P with bogus offset"; return 0; }
    uint8_t vtype2 = pop_val_raw();
    uint8_t vtype = VM_TYPE_NONE; 
    int fudge = get_local(offset-1, vtype);
    if(err != NULL) return 0;
    if(vtype != VM_TYPE_STR && vtype != VM_TYPE_KEY && vtype != VM_TYPE_LIST) {
      err = "RDL_P from wrong type"; return 0; 
    }
    if(caj_vm_check_types(vtype, vtype2)) { err = "WRL_P type mismatch"; return 0; }
    return fudge;
  }

  int get_max_stack(void) {
    return max_stack_used;
  }
};

#define VM_MAGIC 0xcab17ecdUL
#define VM_SECT_BYTECODE 0xca17b17eUL
#define VM_SECT_HEAP 0xca17da7aUL
#define VM_SECT_GLOBALS 0xca17610bUL
#define VM_SECT_FUNCS 0xca17ca11UL
#define VM_MAGIC_END 0xcabcde0dUL
#define VM_VALID_SECT_ID(id) ( ((id) & 0xffff0000UL) == 0xca170000UL)

class vm_serialiser {
 private:
  std::vector<vm_heap_entry> heap;
  std::vector<vm_function*> funcs;
  unsigned char* data; int data_len, data_alloc;
  uint16_t* bytecode; uint32_t bytecode_len;
  int32_t *gvals; uint16_t gvals_len;
  uint32_t *gptrs; uint16_t gptrs_len;
  int sect_start;

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
 vm_serialiser() : data(NULL), bytecode(NULL), gvals(NULL), gptrs(NULL),
    sect_start(0) {
     
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

  void begin_sect(uint32_t magic) {
    assert(sect_start == 0);
    write_u32(magic); sect_start = data_len;
    write_u32(0); // section length;
  }

  void end_sect() {
    assert(sect_start != 0);
    uint32_t len = data_len - (sect_start+4);
    data[sect_start] = (len >> 24) & 0xff;
    data[sect_start+1] = (len >> 16) & 0xff;
    data[sect_start+2] = (len >> 8) & 0xff;
    data[sect_start+3] = (len) & 0xff;
    sect_start = 0;
  }

  unsigned char* serialise(size_t *len) {
    free(data); data = NULL;
    assert(gvals != NULL); assert(gptrs != NULL); assert(bytecode != NULL);

    data_len = 0; data_alloc = 256;
    data = (unsigned char*)malloc(data_alloc);
    write_u32(VM_MAGIC);

    // write heap
    begin_sect(VM_SECT_HEAP);
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
    end_sect();
    
    // write globals
    begin_sect(VM_SECT_GLOBALS);
    write_u16(gvals_len);
    for(unsigned int i = 0; i < gvals_len; i++) {
      write_u32((uint32_t)gvals[i]);
    }
    write_u16(gptrs_len);
    for(unsigned int i = 0; i < gptrs_len; i++) {
      write_u32(gptrs[i]);
    }
    end_sect();

    begin_sect(VM_SECT_FUNCS);
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
      if(funcs[i]->insn_ptr & 0x80000000) write_u32(0);
      else write_u32(funcs[i]->insn_ptr);
    }
    end_sect();

    // write bytecode
    begin_sect(VM_SECT_BYTECODE);
    for(unsigned int i = 0; i < bytecode_len; i++) {
      write_u16(bytecode[i]);
    }
    end_sect();

    write_u32(VM_MAGIC_END);
    
    *len = data_len;
    return data;
  }
};

#endif
