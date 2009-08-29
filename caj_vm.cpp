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
// #include "caj_vm_asm.h"
#include "caj_vm_exec.h"
#include <cassert>


struct script_state {
  uint32_t ip;
  uint32_t mem_use;
  uint32_t bytecode_len;
  uint16_t num_gvals, num_gptrs;
  uint16_t num_funcs;
  uint16_t* bytecode;
  int32_t* stack_top;
  int32_t* frame;
  int32_t* gvals;
  uint32_t* gptrs;
  vm_function *funcs;
  int scram_flag;
};



static script_state *new_script(void) {
  script_state *st = new script_state();
  st->ip = 0;  st->mem_use = 0; st->scram_flag = 0;
  st->bytecode_len = 0; 
  st->num_gvals = st->num_gptrs = st->num_funcs = 0;
  st->bytecode = NULL; st->stack_top = NULL;
  st->frame = NULL; st->gvals = NULL;
  st->gptrs = NULL; st->funcs = NULL;
  return st;
}

struct heap_header {
  uint32_t refcnt;
  uint32_t len;
};

static heap_header *script_alloc(script_state *st, uint32_t len, uint8_t vtype) {
  uint32_t hlen = len + sizeof(heap_header);
  if(len > VM_LIMIT_HEAP || (st->mem_use+hlen) > VM_LIMIT_HEAP) {
    printf("DEBUG: exceeded mem limit of %i allocating %i with %i in use\n",
	   VM_LIMIT_HEAP, (int)len, (int)st->mem_use);
    st->scram_flag = 1; return NULL;
  }
  heap_header* p = (heap_header*)malloc(hlen);
  p->refcnt = ((uint32_t)vtype << 24) | 1;
  p->len = len;
  st->mem_use += hlen;
  return p;
}

static inline void *script_getptr(heap_header *p) {
  return p+1;
}


static  int vtype_size(uint8_t vtype) {
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

class script_loader {
private:
  script_state *st;
  unsigned char *data; int data_len, pos;
  int has_failed;
  uint32_t heap_count;
  vm_heap_entry *heap;

  uint32_t read_u32() {
    if(pos+4 > data_len) { 
      printf("SCRIPT LOAD ERR: overran buffer end\n"); 
      has_failed = 1; return 0;
    }
    uint32_t ret = ((uint32_t)data[pos] << 24) | ((uint32_t)data[pos+1] << 16) |
      ((uint32_t)data[pos+2] << 8) | (uint32_t)data[pos+3];
    pos += 4; return ret;
  }

  uint16_t read_u16() {
    if(pos+2 > data_len) { 
      printf("SCRIPT LOAD ERR: overran buffer end\n"); 
      has_failed = 1; return 0;
    }
    uint32_t ret =  ((uint16_t)data[pos] << 8) | (uint16_t)data[pos+1];
    pos += 2; return ret;
  }

  uint16_t read_u8() {
    if(pos+1 > data_len) { 
      printf("SCRIPT LOAD ERR: overran buffer end\n"); 
      has_failed = 1; return 0;
    }
    return data[pos++];
  }

  void read_data(void* buf, int len) {
    if(pos+len > data_len) { 
      printf("SCRIPT LOAD ERR: overran buffer end\n"); 
      has_failed = 1; return;
    }
    memcpy(buf,data+pos,len); pos += len;
  }

public:
  script_loader() : st(NULL), heap(NULL) {
    
  }

  script_state *load(unsigned char* dat, int len) {
    data = dat; data_len = len; pos = 0; has_failed = false;

    if(read_u32() != 0xf0b17ecd || has_failed) {
      printf("SCRIPT LOAD ERR: bad magic\n"); return NULL;
    }

    st = new_script();

    uint32_t hcount = read_u32();
    if(has_failed) return NULL;
    if(hcount > VM_LIMIT_HEAP_ENTRIES)  {
      printf("SCRIPT LOAD ERR: too many heap entries\n"); return NULL;
    }
    printf("DEBUG: %u heap entries\n", (unsigned)hcount);

    heap = new vm_heap_entry[hcount];

    // FIXME - don't really need the vm_heap_entry struct!
    for(heap_count = 0; heap_count < hcount;) {
      heap[heap_count].vtype = read_u8();
      if(heap[heap_count].vtype > VM_TYPE_MAX) { 
	printf("SCRIPT LOAD ERR: bad vtype\n"); return NULL;
      }
      uint32_t it_len = heap[heap_count].len = read_u32();
      heap_header *p = script_alloc(st, it_len, heap[heap_count].vtype);
      if(p == NULL) { printf("SCRIPT LOAD ERR: memory limit\n"); return NULL; }
      switch(heap[heap_count].vtype) {
      case VM_TYPE_STR:
      default: // FIXME - handle lists, etc
	read_data(script_getptr(p), it_len);
      }
      heap_count++; // placement important for proper mem freeing later
      if(has_failed) return NULL;
    }
  
    
    uint16_t gcnt = read_u16();
    if(has_failed) return NULL;
  
    if(gcnt > VM_MAX_GVALS) {
      printf("SCRIPT LOAD ERR: excess gvals\n"); return NULL;
    }
    printf("DEBUG: %i gvals\n", (int)gcnt);

    st->gvals = new int32_t[gcnt];
    for(unsigned int i = 0; i < gcnt; i++) {
      st->gvals[i] = read_u32();
    }
    if(has_failed) return NULL;
    st->num_gvals = gcnt;
    
    // FIXME - do something with gptrs
    gcnt = read_u16();
    if(has_failed) return NULL;
    if(gcnt > VM_MAX_GPTRS) {
      printf("SCRIPT LOAD ERR: excess gptrs\n"); return NULL;
    }
    printf("DEBUG: %i gptrs\n", (int)gcnt);
    for(unsigned int i = 0; i < gcnt; i++) {
      uint32_t gptr = read_u32();
    }
    if(has_failed) return NULL;

    gcnt = read_u16();
    if(has_failed) return NULL;
    if(gcnt > VM_MAX_FUNCS) {
      printf("SCRIPT LOAD ERR: excess funcs\n"); return NULL;
    }
    printf("DEBUG: %i funcs\n", (int)gcnt);
    st->funcs = new vm_function[gcnt]; st->num_funcs = 0;

    for(unsigned int i = 0; i < gcnt; i++) {
      st->funcs[i].ret_type = read_u8(); //(funcs[i].ret_type);
      if(st->funcs[i].ret_type > VM_TYPE_MAX) { 
	printf("SCRIPT LOAD ERR: bad vtype\n"); return NULL;
      }

      int arg_count = st->funcs[i].arg_count = read_u8();
      st->funcs[i].arg_types = new uint8_t[arg_count];
      st->funcs[i].name = NULL; st->num_funcs++; // for eventual freeing

      st->funcs[i].frame_sz = 1;
      for(int j = 0; j < arg_count; j++) {
	uint8_t arg_type = st->funcs[i].arg_types[j] = read_u8();
	if(arg_type > VM_TYPE_MAX) { 
	  printf("SCRIPT LOAD ERR: bad vtype\n"); return NULL;
	}
	st->funcs[i].frame_sz += vtype_size(arg_type);
      }
      
      int slen = read_u8();
      if(has_failed) return NULL;

      char *name = new char[slen+1];
      read_data(name, slen); name[slen] = 0;
      st->funcs[i].name = name;
      st->funcs[i].insn_ptr = read_u32();
      if(has_failed) return NULL;
    }

    
    st->bytecode_len = read_u32();
    if(has_failed) return NULL;
    if(st->bytecode_len > VM_LIMIT_INSNS) { 
      printf("SCRIPT LOAD ERR: too much bytecode\n"); return NULL;
    }
    st->bytecode = new uint16_t[st->bytecode_len];
    for(unsigned int i = 0; i < st->bytecode_len; i++) {
      st->bytecode[i] = read_u16();
      if(has_failed) return NULL;
    }

    return st; // FIXME - should set st to null
  }
};

script_state* vm_load_script(void* data, int data_len) {
  script_loader loader;
  return loader.load((unsigned char*)data, data_len);
}

static void step_script(script_state* st, int num_steps) {
  uint16_t* bytecode = st->bytecode;
  int32_t* stack_top = st->stack_top;
  uint32_t ip = st->ip;
  for( ; num_steps > 0 && ip != 0; num_steps--) {
    //printf("DEBUG: executing at %u: 0x%04x\n", ip, (int)bytecode[ip]);
    uint16_t insn = bytecode[ip++];
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
      case INSN_DROP_I:
	stack_top++; break;
      case INSN_QUIT:
	ip = 0; goto out;
      case INSN_PRINT_I:
	printf("DEBUG: int %i\n", (int)*(++stack_top));
	break;
      case INSN_PRINT_F:
	printf("DEBUG: float %f\n", (double)*(float*)(++stack_top));
	break;
#if 0 // FIXME
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
#endif
      case INSN_CAST_I2F:
	((float*)stack_top)[1] = stack_top[1];
	break;
      case INSN_CAST_F2I:
	stack_top[1] = ((float*)stack_top)[1];
	break;
	/* FIXME - implement other casts */
      case INSN_BEGIN_CALL:
	// --stack_top; // the magic is in the verifier.
	*(stack_top--) = 0x1231234; // for debugging
	break;
      case INSN_INC_I:
	stack_top[1]++; break;
      case INSN_DEC_I:
	stack_top[1]--; break;
      default:
	 printf("ERROR: unhandled opcode; insn 0x%04x\n",(int)insn);
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
    case ICLASS_CALL:
      {
	int16_t ival = GET_IVAL(insn);
	assert(stack_top[st->funcs[ival].frame_sz] == 0x1231234);
	stack_top[st->funcs[ival].frame_sz] = ip;
	ip = st->funcs[ival].insn_ptr;
      }
      break;
    case ICLASS_RDG_I:
      *(stack_top--) = st->gvals[GET_IVAL(insn)];
      break;
    case ICLASS_WRG_I:
      st->gvals[GET_IVAL(insn)] = *(++stack_top);
      break;
#if 0 // FIXME
    case ICLASS_RDG_P:
      {
	int32_t tmp = st->globals[GET_IVAL(insn)];
	st->heap[tmp]++;
	*(stack_top--) = tmp;
      }
    // TODO - other global-related instructions
#endif
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
      printf("ERROR: unhandled insn class; insn 0x%04x\n",(int)insn);
      goto abort_exec;
    }
  }
 out:
  st->stack_top = stack_top;
  st->ip = ip;
  return; // FIXME;
 abort_exec:
  printf("DEBUG: abborting code execution\n");
  st->ip = 0; st->scram_flag = 1;
}

void caj_vm_test(script_state *st) {
  int32_t stack[128];
  stack[127] = 0;
  st->frame = st->stack_top = stack+126;
  st->ip = 1;
  step_script(st, 1000);
}


#if 0
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
#endif
