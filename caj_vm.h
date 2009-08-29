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
#ifndef CAJ_VM_H
#define CAJ_VM_H

#define ICLASS_NORMAL 0
#define ICLASS_JUMP   1
#define ICLASS_RDG_I  2
#define ICLASS_WRG_I  3
#define ICLASS_RDG_P  4
#define ICLASS_WRG_P  5 // NOT IMPLEMENTED YET
// #define ICLASS_RDG_V  6
// #define ICLASS_WRG_V  7
#define ICLASS_RDL_I 8
#define ICLASS_WRL_I 9
#define ICLASS_RDL_P  10 // NOT IMPLEMENTED YET
#define ICLASS_WRL_P  11 // NOT IMPLEMENTED YET
// #define ICLASS_RDL_V  12
// #define ICLASS_WRL_V  13
#define ICLASS_CALL 14

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


// FIXME - remove this. It doesn't really exist anymore.
#define INSN_QUIT 0xff0


#define VM_TYPE_NONE  0
#define VM_TYPE_INT   1
#define VM_TYPE_FLOAT 2
#define VM_TYPE_STR   3
#define VM_TYPE_KEY   4
#define VM_TYPE_VECT  5
#define VM_TYPE_ROT   6
#define VM_TYPE_LIST  7

#define VM_TYPE_MAX   7
// now for the internal types
#define VM_TYPE_RET_ADDR 100 // for functions we're calling
#define VM_TYPE_OUR_RET_ADDR 101 // for ourselves

/* WARNING WARING - this needs fixing if we increase the number of types */
/* For this reason, do NOT use this for anything that's serialised. Really. */
#define MK_VM_TYPE_PAIR(a, b) (((a) << 3) | b)


// various limits set by the opcode format - FIXME enforce these
#define VM_MAX_GVALS 4095 // max global value variables
#define VM_MAX_GPTRS 4095 // max global pointer vars
#define VM_MAX_LOCALS 2047 // max local vars. Opcodes support more, but issues with 64-bit mode
#define VM_MAX_FUNCS 4095 // max functions
// below are due to encoding
#define VM_MAX_ARGS 255
#define VM_MAX_FUNC_NAME 255

// artificial limits - FIXME enforce these too
#define VM_LIMIT_HEAP 65536
#define VM_LIMIT_HEAP_ENTRIES (65536/8)
#define VM_LIMIT_INSNS 16384 // too low?

struct insn_info {
  uint8_t special, arg1, arg2, ret;
};

#define IVERIFY_INVALID 0
#define IVERIFY_NORMAL 1
#define IVERIFY_COND 2
#define IVERIFY_RET 3

#define IN_CAJ_VM_H
#include "caj_vm_insns.h"
#undef IN_CAJ_VM_H

struct vm_heap_entry {
  uint8_t vtype;
  uint32_t len;
  unsigned char* data;
};

#define VM_MAGIC 0xf0b17ecd

#include <stdlib.h>
#include <cassert>

struct vm_function {
  char* name;
  uint8_t ret_type;
  uint16_t func_num; // only used by vm_asm
  uint32_t insn_ptr; 
  int arg_count;
  uint8_t* arg_types;
  uint16_t* arg_offsets; // only used by vm_asm
  int frame_sz;
};

struct script_state;

script_state* vm_load_script(void* data, int data_len);

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
