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
#include "caj_vm_internal.h"
#include <cassert>

struct heap_header {
  uint32_t refcnt;
  uint32_t len;
};

static uint8_t heap_entry_vtype(heap_header *hentry) {
  return hentry->refcnt >> 24;
}

struct script_state {
  uint32_t ip;
  uint32_t mem_use;
  uint32_t bytecode_len;
  uint16_t num_gvals, num_gptrs;
  uint16_t num_funcs;
  uint16_t* bytecode;
  int32_t *stack_start, *stack_top;
  int32_t* gvals;
  heap_header** gptrs;
  uint8_t* gptr_types;
  vm_function *funcs;
  vm_native_func_cb *nfuncs;
  void *priv; // for the user of the VM
  int scram_flag;
};

static int verify_code(script_state *st);

static inline int ptr_stack_sz(void) {
  if(sizeof(uint32_t) == sizeof(void*)) 
    return 1;
  else if(sizeof(uint32_t)*2 == sizeof(void*)) 
    return 2;
  else assert(0);
}

static script_state *new_script(void) {
  script_state *st = new script_state();
  st->ip = 0;  st->mem_use = 0; st->scram_flag = 0;
  st->bytecode_len = 0; 
  st->num_gvals = st->num_gptrs = st->num_funcs = 0;
  st->bytecode = NULL; st->nfuncs = NULL;
  st->stack_start = st->stack_top = NULL;
  st->gvals = NULL; st->gptr_types = NULL;
  st->gptrs = NULL; st->funcs = NULL;
  return st;
}


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

static void heap_ref_decr(heap_header *p, script_state *st) {
  if( ((--(p->refcnt)) & 0xffffff) == 0) {
    printf("DEBUG: freeing heap entry 0x%p\n",p);
    st->mem_use -= p->len + sizeof(heap_header);
    free(p);
  }
}

static inline void heap_ref_incr(heap_header *p) {
  p->refcnt++;
}

static inline uint32_t heap_get_refcnt(heap_header *p) {
  return p->refcnt & 0xffffff;
}

static inline void *script_getptr(heap_header *p) {
  return p+1;
}

void vm_free_script(script_state * st) {
  if(st->stack_start != NULL) {
    // FIXME - need to find pointers on stack and free them...
    printf("WARNING: vm_free_script doesn't free script stacks right yet\n");
  }
  for(unsigned i = 0; i < st->num_gptrs; i++) {
    heap_header *p = st->gptrs[i];
    if(heap_get_refcnt(p) != 1) {
      // won't apply once we support lists
      printf("WARNING: unexpected refcnt for 0x%p on script shutdown: %u\n",
	     p, (unsigned)heap_get_refcnt(p));
    }
    heap_ref_decr(p, st);
  }
  delete[] st->gvals; delete[] st->gptrs; delete[] st->gptr_types;
  delete[] st->bytecode; // FIXME - will want to add bytecode sharing

  for(unsigned i = 0; i < st->num_funcs; i++) {
    delete[] st->funcs[i].arg_types; delete[] st->funcs[i].name;
  }
  delete[] st->funcs;
  delete[] st->stack_start;
  delete st;
}


static int vm_vtype_size(uint8_t vtype) { // not same as vm_asm equivalent
  switch(vtype) {
  case VM_TYPE_NONE:
    return 0; // for return values, mainly
  case VM_TYPE_INT:
  case VM_TYPE_FLOAT:
    return 1;
  case VM_TYPE_STR:
  case VM_TYPE_KEY:
  case VM_TYPE_LIST:
    return ptr_stack_sz();
  case VM_TYPE_VECT:
    return 3;
  case VM_TYPE_ROT:
    return 4;
  default: printf("ERROR: bad vtype in vm_vtype_size()\n"); abort();
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

  void free_our_heap() {
    if(heap != NULL) {
      assert(st != NULL);

      for(uint32_t i = 0; i < heap_count; i++) {
	heap_ref_decr((heap_header*) heap[i].data, st); // FIXME bad types
      }
      delete[] heap; heap = NULL;
    }
  }

public:
  script_loader() : st(NULL), heap(NULL) {
    
  }
  
  ~script_loader() {
    free_our_heap();
    if(st != NULL) vm_free_script(st);
  }

  script_state *load(unsigned char* dat, int len) {
    data = dat; data_len = len; pos = 0; has_failed = false;

    if(read_u32() != 0xf0b17ecd || has_failed) {
      printf("SCRIPT LOAD ERR: bad magic\n"); return NULL;
    }

    free_our_heap();
    if(st != NULL) vm_free_script(st);
    st = new_script();

    // first, read the heap
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
      heap[heap_count].data = (unsigned char*)p; // FIXME - bad types!
      heap_count++; // placement important for proper mem freeing later
      if(has_failed) return NULL;
    }
  
   
    { 
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
    }

    {
      uint16_t gcnt = read_u16();
      if(has_failed) return NULL;
      if(gcnt > VM_MAX_GPTRS) {
	printf("SCRIPT LOAD ERR: excess gptrs\n"); return NULL;
      }

      st->gptrs = new heap_header*[gcnt]; st->num_gptrs = 0;
      st->gptr_types = new uint8_t[gcnt];
      printf("DEBUG: %i gptrs\n", (int)gcnt);

      for(unsigned int i = 0; i < gcnt; i++) {
	uint32_t gptr = read_u32();
	if(has_failed) return NULL;
	if(gptr >= heap_count) {
	  printf("SCRIPT LOAD ERR: invalid gptr\n"); return NULL;
	}
	heap_header *p = (heap_header*)heap[gptr].data; // FIXME
	heap_ref_incr(p);
	st->gptrs[i] = p; st->gptr_types[i] = heap_entry_vtype(p);
	st->num_gptrs++;
      }
      if(has_failed) return NULL;
    }

    {
      uint16_t gcnt = read_u16();
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
	  st->funcs[i].frame_sz += vm_vtype_size(arg_type);
	}
      
	int slen = read_u8();
	if(has_failed) return NULL;

	char *name = new char[slen+1];
	read_data(name, slen); name[slen] = 0;
	st->funcs[i].name = name;
	st->funcs[i].insn_ptr = read_u32();
	if(has_failed) return NULL;
      }
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

    if(!verify_code(st)) {
      printf("SCRIPT LOAD ERR: didn't verify\n"); return NULL;
    };
    
    { // final return
      script_state *st2 = st; free_our_heap(); st = NULL;
      return st2;
    }
  }
};

script_state* vm_load_script(void* data, int data_len) {
  script_loader loader;
  return loader.load((unsigned char*)data, data_len);
}

static int verify_pass1(unsigned char * visited, uint16_t *bytecode, vm_function *func) {
  std::vector<uint32_t> pending;
  pending.push_back(func->insn_ptr);
 next_chunk:
  while(!pending.empty()) {
    uint32_t ip = pending.back(); pending.pop_back();
    for(;;) {
      if(ip < func->insn_ptr || ip >= func->insn_end) {
	printf("SCRIPT VERIFY ERR: IP out of bounds\n"); return 0;
      }
      if(visited[ip] != 0) { 
	visited[ip] = 2; goto next_chunk;
      }

      visited[ip] = 1; uint16_t insn = bytecode[ip++];
      
      switch(GET_ICLASS(insn)) {
      case ICLASS_NORMAL:
	{
	  uint16_t ival = GET_IVAL(insn);
	  if(ival >= NUM_INSNS) { 
	    printf("SCRIPT VERIFY ERR: invalid instruction\n"); return 0; 
	  }
	  switch(vm_insns[ival].special) {
	  case IVERIFY_INVALID: 
	    printf("SCRIPT VERIFY ERR: invalid instruction\n"); return 0; 
	  case IVERIFY_RET:
	    goto next_chunk;
	  case IVERIFY_COND:
	    // execution could skip the next instruction...
	    pending.push_back(ip+1);
	    break;
	  case IVERIFY_NORMAL: // not interesting yet
	  default: break;
	  }
	  break;
	}
      case ICLASS_JUMP:
	{
	  uint16_t ival = GET_IVAL(insn);
	  if(ival & 0x800) {
	    ip -= ival & 0x7ff;
	  } else {
	    ip += ival;
	  }
	}
	break;
      default: break;
      }
    }
  }
  
  return 1;
}

struct pass2_state {
  uint32_t ip;
  struct asm_verify* verify;

  pass2_state(uint32_t ipstart, struct asm_verify* v) : ip(ipstart), verify(v) {
  }
};

static int verify_pass2(unsigned char * visited, uint16_t *bytecode, vm_function *func,
			script_state *st) {
  std::vector<pass2_state> pending; const char* err = NULL;
  std::map<uint32_t,asm_verify*> done;
  {
    asm_verify* verify = new asm_verify(err, func);
    pending.push_back(pass2_state(func->insn_ptr, verify));
  }
 next_chunk:
  while(!pending.empty()) {
    pass2_state vs = pending.back(); pending.pop_back();
    for(;;) {
      assert(!(vs.ip < func->insn_ptr || vs.ip >= func->insn_end)); // checked pass 1
      assert(visited[vs.ip] != 0); 

      if(visited[vs.ip] > 1) { 
	std::map<uint32_t,asm_verify*>::iterator iter = done.find(vs.ip);
	if(iter == done.end()) {
	  done[vs.ip] = vs.verify->dup();
	} else {
	  vs.verify->combine_verify(iter->second); delete vs.verify;
	  if(err != NULL) goto out; else goto next_chunk;
	}
      }


      uint16_t insn = bytecode[vs.ip];
      // printf("DEBUG: verifying 0x%04x @ %i\n", (unsigned)insn, (int)vs.ip);
      uint32_t next_ip = vs.ip+1;
      
      switch(GET_ICLASS(insn)) {
      case ICLASS_NORMAL:
	{
	  uint16_t ival = GET_IVAL(insn);
	  assert(ival < NUM_INSNS); // checked pass 1

	  insn_info info = vm_insns[ival];
	  vs.verify->pop_val(info.arg1); 
	  vs.verify->pop_val(info.arg2);
	  vs.verify->push_val(info.ret);
	  if(err != NULL) { delete vs.verify; goto out; }

	  switch(info.special) {
	  case IVERIFY_INVALID: 
	    assert(0); break; // checked pass 1
	  case IVERIFY_RET:
	    delete vs.verify; goto next_chunk;
	  case IVERIFY_COND:
	    // execution could skip the next instruction...
	    pending.push_back(pass2_state(next_ip+1, vs.verify->dup()));
	    break;
	  case IVERIFY_NORMAL: // not interesting yet
	  default: break;
	  }
	  break;
	}
      case ICLASS_RDL_I:
	{
	  int16_t ival = GET_IVAL(insn);
	  int fudge = vs.verify->check_rdl_i(ival)*(ptr_stack_sz()-1);
	  if(err != NULL) { delete vs.verify; goto out; }
	  if(fudge + ival >= VM_MAX_IVAL) {
	    err = "64-bit fudge exceeds max ival. Try on a 32-bit VM?";
	    delete vs.verify; goto out;
	  };
	  if(fudge > 0) {
	    // FIXME - record fudge factors somewhere
	    bytecode[vs.ip] += fudge;
	  }
	  break;
	}
      case ICLASS_WRL_I:
	{
	  int16_t ival = GET_IVAL(insn);
	  int fudge = vs.verify->check_wrl_i(ival)*(ptr_stack_sz()-1);
	  if(err != NULL) { delete vs.verify; goto out; }
	  if(fudge + ival >= VM_MAX_IVAL) {
	    err = "64-bit fudge exceeds max ival. Try on a 32-bit VM?";
	    delete vs.verify; goto out;
	  };
	  if(fudge > 0) {
	    // FIXME - record fudge factors somewhere
	    bytecode[vs.ip] += fudge;
	  }
	  break;
	}
      case ICLASS_RDG_I:
	{
	  int16_t ival = GET_IVAL(insn); 
	  if(ival >= st->num_gvals) {
	    err = "Bad global variable read";
	  } else {
	    vs.verify->push_val(VM_TYPE_INT);
	  }
	  break;
	}
      case ICLASS_WRG_I:
	{
	  int16_t ival = GET_IVAL(insn);
	  if(ival >= st->num_gvals) {
	    err = "Bad global variable write"; 
	  } else {
	    vs.verify->pop_val(VM_TYPE_INT);
	  }
	  break;
	}
      case ICLASS_RDG_P:
	{
	  int16_t ival = GET_IVAL(insn); 
	  if(ival >= st->num_gptrs) {
	    err = "Bad global pointer read";
	  } else {
	    vs.verify->push_val(st->gptr_types[ival]);
	  }
	  break;
	}
      case ICLASS_WRG_P:
	{
	  int16_t ival = GET_IVAL(insn); 
	  if(ival >= st->num_gptrs) {
	    err = "Bad global pointer write";
	  } else {
	    vs.verify->pop_val(st->gptr_types[ival]);
	  }
	  break;
	}	
      case ICLASS_JUMP:
	{
	  uint16_t ival = GET_IVAL(insn);
	  if(ival & 0x800) {
	    next_ip -= ival & 0x7ff;
	  } else {
	    next_ip += ival;
	  }
	}
	break;
      case ICLASS_CALL:
	{
	  uint16_t ival = GET_IVAL(insn);
	  if(ival >= st->num_funcs) {
	    err = "Call to invalid function number"; 
	    delete vs.verify; goto out;
	  }
	  vm_function *func = &st->funcs[ival];
	  for(int i = func->arg_count - 1; i >= 0; i--) {
	    vs.verify->pop_val(func->arg_types[i]);
	  }
	  vs.verify->pop_val(VM_TYPE_RET_ADDR);
	  // bit hacky, but should work...
	  vs.verify->pop_val(func->ret_type);
	  vs.verify->push_val(func->ret_type);
	}
	break;
      default:
	printf("DEBUG: insn 0x%x\n", (unsigned)insn);
	err = "unhandled iclass"; break;
      }
      if(err != NULL) { delete vs.verify; goto out; };
      // vs.verify->dump_stack("  ");
      vs.ip = next_ip;
    }
  }
  
 out:
  // FIXME - free memory allocated in pending!

  for(std::map<uint32_t,asm_verify*>::iterator iter = done.begin(); 
      iter != done.end(); iter++) {
    delete iter->second;
  }

  if(err != NULL) { printf("SCRIPT VERIFY ERR: %s\n", err); return 0; }
  return 1;
}

// !!!!!!!!! FIXME !!!!!!!!!!!!
// We need to prevent the user overflowing the stack. This has nasty security
// implications (probably arbitrary code execution, if they play their cards
// right!) 
// !!!!!!!!!!!!!!!!!!!!!!!!!!!!
static int verify_code(script_state *st) {
  if(st->stack_top != NULL) return 0;
  {
    uint32_t last_ip = 0; int last_func = -1;
    for(int i = 0; i < st->num_funcs; i++) {
      if(st->funcs[i].insn_ptr == 0) continue;
      if(st->funcs[i].insn_ptr <= last_ip) {
	printf("SCRIPT VERIFY ERR: functions in wrong order\n");
	return 0;
      } else if(st->funcs[i].insn_ptr >= st->bytecode_len) {
	printf("SCRIPT VERIFY ERR: function has invalid bytecode ptr\n");
	return 0;
      }
      if(last_func >= 0) st->funcs[last_func].insn_end = st->funcs[i].insn_ptr;
      last_ip = st->funcs[i].insn_ptr; last_func = i;
    }
    if(last_func >= 0) st->funcs[last_func].insn_end = st->bytecode_len;
  }

  // FIXME - need to verify first insn is INSN_QUIT!

  unsigned char *visited = new uint8_t[st->bytecode_len];
  memset(visited, 0, st->bytecode_len);
  for(int i = 0; i < st->num_funcs; i++) {
    if(st->funcs[i].insn_ptr == 0) continue;
    if(!verify_pass1(visited, st->bytecode, &st->funcs[i])) goto out_fail;
    if(!verify_pass2(visited, st->bytecode, &st->funcs[i], st)) goto out_fail;
  }

  delete[] visited;  return 1;
 out_fail:
  delete[] visited; return 0;
}

static void put_stk_ptr(int32_t *tloc, heap_header* p) {  
  union { heap_header* p; uint32_t v[2]; } u; // HACK
  uint32_t *loc = (uint32_t*)tloc;

  if(sizeof(heap_header*) == sizeof(uint32_t)) {
    // loc[0] = (uint32_t)p; // doesn't compile on 64-bit systems
    u.p = p; loc[0] = u.v[0]; // HACK HACK HACK
  } else if(sizeof(heap_header*) == 2*sizeof(uint32_t)) {
    union { heap_header* p; uint32_t v[2]; } u; // HACK
    assert(sizeof(u) == sizeof(heap_header*));
    u.p = p; loc[0] = u.v[0]; loc[1] = u.v[1];
  }
}

static heap_header* get_stk_ptr(int32_t *tloc) {
  union { heap_header* p; uint32_t v[2]; } u; // HACK
  uint32_t *loc = (uint32_t*)tloc;

  if(sizeof(heap_header*) == sizeof(uint32_t)) {
    // return (heap_header*) loc[0]; // ditto
    u.v[0] = loc[0]; return u.p; // HACK HACK HACK
  } else if(sizeof(heap_header*) == 2*sizeof(uint32_t)) {
    assert(sizeof(u) == sizeof(heap_header*));
    u.v[0] = loc[0]; u.v[1] = loc[1]; return u.p;
  }
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
      // TODO: INSN_DROP_P
      case INSN_DROP_I3:
	stack_top += 3; break;
      case INSN_DROP_I4:
	stack_top += 4; break;
      case INSN_QUIT: // dirty dirty hack!
	ip = 0; goto out;
      case INSN_PRINT_I:
	printf("DEBUG: int %i\n", (int)*(++stack_top));
	break;
      case INSN_PRINT_F:
	printf("DEBUG: float %f\n", (double)*(float*)(++stack_top));
	break;
      case INSN_PRINT_STR:
	{
	  heap_header *p = get_stk_ptr(stack_top+1);
	  int32_t len = p->len;
	  char buf[len+1];  // HACK!
	  memcpy(buf, script_getptr(p), len);
	  buf[len] = 0;
	  printf("DEBUG: string '%s'\n", buf);
	  heap_ref_decr(p, st); stack_top += ptr_stack_sz();
	  break;
	}
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
	if(ip == 0) goto abort_exec; // unbound native function
      }
      break;
    case ICLASS_RDG_I:
      *(stack_top--) = st->gvals[GET_IVAL(insn)];
      break;
    case ICLASS_WRG_I:
      st->gvals[GET_IVAL(insn)] = *(++stack_top);
      break;
    case ICLASS_RDG_P:
      {
	heap_header *p = st->gptrs[GET_IVAL(insn)];  
	stack_top -= ptr_stack_sz(); heap_ref_incr(p);
	put_stk_ptr(stack_top+1,p);
	break;
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

int vm_script_is_idle(script_state *st) {
  return st->ip == 0 && st->scram_flag == 0;
}

int vm_script_is_runnable(script_state *st) {
  return st->ip == 0 && st->scram_flag == 0;
}

// the native funcs array is generally global so we don't free it
void vm_prepare_script(script_state *st, void *priv, vm_native_func_cb* nfuncs,
		       vm_get_func_cb get_func) {
  assert(st->stack_top == NULL);
  st->stack_start = new int32_t[1024]; // FIXME - pick this properly;
  st->stack_top = st->stack_start+1023;
  st->nfuncs = nfuncs; st->priv = priv;
 
  // FIXME - need to bind native funcs!
}

void vm_run_script(script_state *st, int num_steps) {
  if(st->scram_flag != 0 || st->ip == 0) return;
  assert(st->stack_top != NULL);
  
}

// FIXME - remove this
void caj_vm_test(script_state *st) {
  int32_t stack[128];
  stack[127] = 0;
  st->stack_top = stack+126;
  st->ip = 1;
  step_script(st, 1000);
}

