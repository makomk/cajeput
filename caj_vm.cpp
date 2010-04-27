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
#include "caj_types.h"
#include <cassert>
#include <stdarg.h>
#include <math.h>

//#define DEBUG_TRACEVALS

typedef uint32_t vm_traceval;
typedef std::vector<uint8_t> traceval_stack;

#define TRACEVAL_COUNT(v) ( ((v) >> 24) & 0xff)
#define TRACEVAL_IP(v) ( (v) & 0xffffff)

struct heap_header {
  uint32_t refcnt;
  uint32_t len;
};

static uint8_t heap_entry_vtype(heap_header *hentry) {
  return hentry->refcnt >> 24;
}

struct vm_nfunc_desc { // native function
  uint8_t ret_type;
  int arg_count;
  uint8_t* arg_types;
  int number;
  vm_native_func_cb cb;
};

struct vm_world {
  std::vector<vm_nfunc_desc> nfuncs;
  std::map<std::string, int> nfunc_map;
  std::map<std::string, vm_nfunc_desc> event_map; // may want to give own type
  int num_events;
};

#define VM_SCRAM_OK 0
#define VM_SCRAM_ERR 1
#define VM_SCRAM_DIV_ZERO 2
#define VM_SCRAM_STACK_OVERFLOW 3
#define VM_SCRAM_BAD_OPCODE 4
#define VM_SCRAM_MISSING_FUNC 5
#define VM_SCRAM_MEM_LIMIT 6

struct script_state {
  uint32_t ip;
  uint32_t mem_use;
  uint32_t bytecode_len;
  uint16_t num_gvals, num_gptrs;
  uint16_t num_funcs;
  uint16_t* bytecode;
  uint16_t* patched_bytecode; // FIXME - only needed on 64-bit systems
  vm_traceval* tracevals;
  int32_t *stack_start, *stack_top;
  int32_t* gvals;
  heap_header** gptrs;
  uint8_t* gptr_types;
  vm_function *funcs;
  vm_native_func_cb *nfuncs;
  uint16_t *cur_state; // event functions for current state
  vm_world *world;
  void *priv; // for the user of the VM
  int scram_flag;
};

static int verify_code(script_state *st);
static void script_calc_stack(script_state *st, traceval_stack &stack);
static void unwind_stack(script_state * st);

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
  st->bytecode = st->patched_bytecode = NULL; 
  st->tracevals = NULL;
  st->nfuncs = NULL;
  st->stack_start = st->stack_top = NULL;
  st->gvals = NULL; st->gptr_types = NULL;
  st->gptrs = NULL; st->funcs = NULL;
  st->cur_state = NULL;
  return st;
}


static heap_header *script_alloc(script_state *st, uint32_t len, uint8_t vtype) {
  uint32_t hlen = len + sizeof(heap_header);
  if(len > VM_LIMIT_HEAP || (st->mem_use+hlen) > VM_LIMIT_HEAP) {
    printf("DEBUG: exceeded mem limit of %i allocating %i with %i in use\n",
	   VM_LIMIT_HEAP, (int)len, (int)st->mem_use);
    st->scram_flag = VM_SCRAM_MEM_LIMIT; return NULL;
  }
  heap_header* p = (heap_header*)malloc(hlen);
  p->refcnt = ((uint32_t)vtype << 24) | 1;
  p->len = len;
  st->mem_use += hlen;
  return p;
}

static heap_header *script_alloc_list(script_state *st, uint32_t len) {
  uint32_t fakelen = len*4 + sizeof(heap_header);
  if(len > VM_LIMIT_HEAP || (st->mem_use+fakelen) > VM_LIMIT_HEAP) {
    printf("DEBUG: exceeded mem limit of %i allocating %i-item list with %i in use\n",
	   VM_LIMIT_HEAP, (int)len, (int)st->mem_use);
    st->scram_flag = VM_SCRAM_MEM_LIMIT; return NULL;
  }
  heap_header* p = (heap_header*)malloc(len*sizeof(heap_header*) + 
					sizeof(heap_header));
  p->refcnt = ((uint32_t)VM_TYPE_LIST << 24) | 1;
  p->len = len;
  st->mem_use += fakelen;
  return p;
}

static inline void *script_getptr(heap_header *p) {
  return p+1;
}

static void heap_ref_decr(heap_header *p, script_state *st) {
  if( ((--(p->refcnt)) & 0xffffff) == 0) {
    // printf("DEBUG: freeing heap entry 0x%p\n",p);
    if(heap_entry_vtype(p) == VM_TYPE_LIST) {
      heap_header **list = (heap_header**)script_getptr(p);
      for(unsigned i = 0; i < p->len; i++)
	heap_ref_decr(list[i], st);
      st->mem_use -= p->len*4 + sizeof(heap_header);
    } else {
      st->mem_use -= p->len + sizeof(heap_header);
    }
    free(p);
  }
}

static inline void heap_ref_incr(heap_header *p) {
  p->refcnt++;
}

static inline uint32_t heap_get_refcnt(heap_header *p) {
  return p->refcnt & 0xffffff;
}

static heap_header* make_vm_string(script_state *st, const char* str) {
  int len = strlen(str);
  heap_header* p = script_alloc(st, len, VM_TYPE_STR);
  if(p != NULL) {
    memcpy(script_getptr(p), str, len);
  }
  return p;
}

static heap_header* make_vm_string(script_state *st, const std::string str) {
  if(str.length() > 0x100000) {
    st->scram_flag = VM_SCRAM_MEM_LIMIT; return NULL;
  }
  int len = str.length();
  heap_header* p = script_alloc(st, len, VM_TYPE_STR);
  if(p != NULL) {
    memcpy(script_getptr(p), str.c_str(), len);
  }
  return p;
}

static heap_header* make_num_on_heap(script_state *st, int32_t *val,
					int vtype, int count) {
  heap_header* p = script_alloc(st, 4*count, vtype);
  if(p != NULL) {
    memcpy(script_getptr(p), val, count*sizeof(int32_t));
  }
  return p;
}

static heap_header* make_single_list(script_state *st, heap_header* item) {
  if(item == NULL) return NULL;
  heap_header* list = script_alloc_list(st, 1);
  if(list != NULL) {
    *(heap_header**)script_getptr(list) = item;
  } else {
    heap_ref_decr(item, st);
  }
  return list;
}


void vm_free_script(script_state * st) {
  if(st->stack_start != NULL && st->ip != 0) {
    unwind_stack(st);
  }
  for(unsigned i = 0; i < st->num_gptrs; i++) {
    heap_header *p = st->gptrs[i]; heap_ref_decr(p, st);
  }
  delete[] st->gvals; delete[] st->gptrs; delete[] st->gptr_types;
  if(st->patched_bytecode != st->bytecode) delete[] st->patched_bytecode;
  delete[] st->bytecode; // FIXME - will want to add bytecode sharing
  delete[] st->tracevals;

  for(unsigned i = 0; i < st->num_funcs; i++) {
    delete[] st->funcs[i].arg_types; delete[] st->funcs[i].name;
  }
  delete[] st->funcs;
  delete[] st->stack_start;
  delete[] st->cur_state;
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

    if(read_u32() != VM_MAGIC || has_failed) {
      printf("SCRIPT LOAD ERR: bad magic\n"); return NULL;
    }

    free_our_heap();
    if(st != NULL) vm_free_script(st);
    st = new_script();

    int is_end = 0;
    while(!is_end) {
      uint32_t sect = read_u32();
      if(has_failed) {
	printf("SCRIPT LOAD ERROR: unexpected EOF reading section\n");
	return NULL;
      } else if(sect == VM_MAGIC_END) {
	is_end = 1; break;
      }

      uint32_t sect_len = read_u32(); int sect_end = pos + sect_len;
      if(has_failed || (uint32_t)(data_len - pos) < sect_len) {
	printf("SCRIPT LOAD ERROR: unexpected EOF reading section\n");
	return NULL;
      } else if(!VM_VALID_SECT_ID(sect)) {
	printf("SCRIPT LOAD ERROR: invalid section ID 0x%x\n", (unsigned)sect);
	return NULL;
      }
  
      switch(sect) {
      case VM_SECT_HEAP:
	{
	  if(heap != NULL) {
	    printf("SCRIPT LOAD ERROR: duplicate heap section\n");
	    return NULL;
	  }

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
	      printf("SCRIPT LOAD ERR: bad vtype %i on heap\n",
		     (int)heap[heap_count].vtype); return NULL;
	    }
	    uint32_t it_len = heap[heap_count].len = read_u32();
	    heap_header *p;
	    // FIXME - handle this right
	    if(heap[heap_count].vtype == VM_TYPE_STR || 
	       heap[heap_count].vtype == VM_TYPE_KEY) { 
	      p = script_alloc(st, it_len, heap[heap_count].vtype);
	      if(p == NULL) { 
		printf("SCRIPT LOAD ERR: memory limit\n"); return NULL;
	      }
	      read_data(script_getptr(p), it_len);
	    } else if(heap[heap_count].vtype == VM_TYPE_LIST) {
	      it_len /= 4;
	      p = script_alloc_list(st, it_len);
	      if(p == NULL) { 
		printf("SCRIPT LOAD ERR: memory limit\n"); return NULL; 
	      }
	      printf("DEBUG: created a list at %p\n", p);
	      p->len = 0; // HACK!!!!
	      heap_header **list = (heap_header**)script_getptr(p);
	      memset(list, 0, it_len*sizeof(heap_header*));
	      for(uint32_t i = 0; i < it_len; i++) {
		uint32_t gptr = read_u32();
		if(has_failed) return NULL;
		if(gptr >= heap_count) {
		  printf("SCRIPT LOAD ERR: invalid gptr\n"); 
		  has_failed = 1; break;
		}
		heap_header *itemp = (heap_header*)heap[gptr].data; // FIXME
		if(heap_entry_vtype(itemp) == VM_TYPE_LIST) {
		  // technically, we don't need to block this... it should be
		  // impossible to load or create a circularly-linked data
		  // structure anyway (immutable data + ordering restrictions 
		  // within the file format). LSL doesn't allow it, though.
		  printf("SCRIPT LOAD ERR: list within a list\n"); 
		  has_failed = 1; break;
		}
		heap_ref_incr(itemp); list[i] = itemp;
		p->len = i+1; // HACK!!!!
	      }
	    } else if(heap[heap_count].vtype == VM_TYPE_INT || 
		      heap[heap_count].vtype == VM_TYPE_FLOAT) { 
	      if(it_len != 4) {
		printf("SCRIPT LOAD ERR: heap int/float not 4 bytes\n");
		return NULL;
	      }
	      p = script_alloc(st, 4, heap[heap_count].vtype);
	      if(p == NULL) { 
		printf("SCRIPT LOAD ERR: memory limit\n"); return NULL;
	      }
	      *(int32_t*)script_getptr(p) = read_u32();
	    } else if(heap[heap_count].vtype == VM_TYPE_VECT || 
		      heap[heap_count].vtype == VM_TYPE_ROT) {
	      int count = heap[heap_count].vtype == VM_TYPE_VECT ? 3 : 4;
	      if(it_len != 4*count) {
		printf("SCRIPT LOAD ERR: heap vect/rot wrong size\n");
		return NULL;
	      }
	      p = script_alloc(st, 4*count, heap[heap_count].vtype);
	      if(p == NULL) { 
		printf("SCRIPT LOAD ERR: memory limit\n"); return NULL;
	      }
	      int32_t *v = (int32_t*)script_getptr(p);
	      for(int i = 0; i < count; i++, v++) {
		*v = read_u32();
	      }
	    } else {
	      printf("SCRIPT LOAD ERR: unhandled heap entry vtype\n"); return NULL;
	    }
	    heap[heap_count].data = (unsigned char*)p; // FIXME - bad types!
	    heap_count++; // placement important for proper mem freeing later
	    if(has_failed) return NULL;
	  }
	}
	if(pos != sect_end) {
	  printf("ERROR: bad length of section\n"); return NULL;
	}
	break;
      case VM_SECT_GLOBALS:
	if(st->gvals != NULL) {
	  printf("SCRIPT LOAD ERROR: duplicate globals section\n");
	  return NULL;
	} else if(heap == NULL) {
	  printf("SCRIPT LOAD ERROR: heap section must precede globals\n");
	  return NULL;
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
	if(pos != sect_end) {
	  printf("ERROR: bad length of section\n"); return NULL;
	}
	break;
      case VM_SECT_FUNCS:
	if(st->funcs != NULL) {
	  printf("SCRIPT LOAD ERROR: duplicate functions section\n");
	  return NULL;
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
	      printf("SCRIPT LOAD ERR: bad vtype %i as ret type\n",
		     (int)st->funcs[i].ret_type); return NULL;
	    }

	    int arg_count = st->funcs[i].arg_count = read_u8();
	    st->funcs[i].arg_types = new uint8_t[arg_count];
	    st->funcs[i].name = NULL; st->num_funcs++; // for eventual freeing

	    st->funcs[i].frame_sz = 1;
	    for(int j = 0; j < arg_count; j++) {
	      uint8_t arg_type = st->funcs[i].arg_types[j] = read_u8();
	      if(arg_type > VM_TYPE_MAX) { 
		printf("SCRIPT LOAD ERR: bad vtyp %i of argument\n",
		       (int)arg_type); return NULL;
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
	if(pos != sect_end) {
	  printf("ERROR: bad length of section\n"); return NULL;
	}
	break;
      case VM_SECT_BYTECODE:
	if(st->bytecode != NULL) {
	  printf("SCRIPT LOAD ERROR: duplicate bytecode section\n");
	  return NULL;
	}

	st->bytecode_len = sect_len/2;
	if(st->bytecode_len > VM_LIMIT_INSNS) { 
	  printf("SCRIPT LOAD ERR: too much bytecode\n"); return NULL;
	}
	st->bytecode = new uint16_t[st->bytecode_len];
	for(unsigned int i = 0; i < st->bytecode_len; i++) {
	  st->bytecode[i] = read_u16();
	  if(has_failed) return NULL;
	}
	if(pos != sect_end) {
	  printf("ERROR: bad length of section\n"); return NULL;
	}
	break;	
      default:
	pos = sect_end; break;
      }
    }

    if(has_failed) return NULL;

    if(heap == NULL || st->gvals == NULL || st->gptrs == NULL ||
       st->funcs == NULL || st->bytecode == NULL) {
      printf("SCRIPT LOAD ERR: missing required section\n"); return NULL;
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

static uint32_t serialise_heap_int(vm_serialiser &serial, heap_header* hptr,
				   unsigned count,
				   std::vector<unsigned char*> &tmpbufs) {
  int32_t *val = (int32_t*)script_getptr(hptr);
  unsigned char *buf = (unsigned char*)malloc(4*count);
  assert(hptr->len == 4*count);

  for(unsigned i = 0; i < count; i++) {
    serial.int_to_bin(val[i], buf+(4*i));
  }
  tmpbufs.push_back(buf);
  return serial.add_heap_entry(heap_entry_vtype(hptr), 4*count, buf);
}

static uint32_t serialise_heap_item(std::map<heap_header*,uint32_t> &heap_map,
				    vm_serialiser &serial, heap_header* hptr,
				    std::vector<unsigned char*> &tmpbufs) {
  std::map<heap_header*,uint32_t>::iterator iter = 
      heap_map.find(hptr);
  uint8_t vtype = heap_entry_vtype(hptr);
  if(iter != heap_map.end()) {
    return iter->second;
  } else if(vtype == VM_TYPE_STR || vtype == VM_TYPE_KEY) {
    uint32_t hidx = serial.add_heap_entry(vtype, hptr->len,
					  script_getptr(hptr));
    heap_map[hptr] = hidx; return hidx;
  } else if(vtype == VM_TYPE_INT || vtype == VM_TYPE_FLOAT) {
    uint32_t hidx = serialise_heap_int(serial, hptr, 1, tmpbufs);
    heap_map[hptr] = hidx; return hidx;
  } else if(vtype == VM_TYPE_VECT) {
    uint32_t hidx = serialise_heap_int(serial, hptr, 3, tmpbufs);
    heap_map[hptr] = hidx; return hidx;
  } else if(vtype == VM_TYPE_ROT) {
    uint32_t hidx = serialise_heap_int(serial, hptr, 4, tmpbufs);
    heap_map[hptr] = hidx; return hidx;
  } else if(vtype == VM_TYPE_LIST) {
    heap_header **items = (heap_header**)script_getptr(hptr);
    unsigned char *buf = (unsigned char*)calloc(hptr->len, 4);
    for(uint32_t i = 0; i < hptr->len; i++) {
      uint32_t item_idx = serialise_heap_item(heap_map, serial, 
					      items[i], tmpbufs);
      serial.int_to_bin(item_idx, buf+(4*i));
    }
    uint32_t hidx = serial.add_heap_entry(vtype, hptr->len*4, buf);
    tmpbufs.push_back(buf);
    heap_map[hptr] = hidx; return hidx;
  } else {
    printf("FATAL ERROR: unhandled heap item type %i\n", vtype);
    // FIXME
    abort(); return 0;
  }
}

// FIXME - really need to serialise stack too...
unsigned char* vm_serialise_script(script_state *st, size_t *len) {
  if(st->scram_flag != 0) {
    *len = 0; return NULL;
  }
  vm_serialiser serial; std::vector<unsigned char*> tmpbufs;
  std::map<heap_header*,uint32_t> heap_map;
  uint32_t *gptrs = new uint32_t[st->num_gptrs];
  for(unsigned i = 0; i < st->num_gptrs; i++) {
    gptrs[i] = serialise_heap_item(heap_map, serial, st->gptrs[i], tmpbufs);
  }

  serial.set_bytecode(st->bytecode, st->bytecode_len);
  serial.set_gvals(st->gvals, st->num_gvals);
  serial.set_gptrs(gptrs, st->num_gptrs);
  for(unsigned i = 0; i < st->num_funcs; i++) {
    serial.add_func(&st->funcs[i]);
  }
  unsigned char *ret = serial.serialise(len);
  for(std::vector<unsigned char*>::iterator iter = tmpbufs.begin();
      iter != tmpbufs.end(); iter++) {
    free(*iter);
  }
  delete[] gptrs; return ret;
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
  uint32_t ip; vm_traceval trace;
  struct asm_verify* verify;

  pass2_state(uint32_t ipstart, vm_traceval trace_now, struct asm_verify* v) : ip(ipstart), trace(trace_now), verify(v) {
  }
};

static inline vm_traceval build_traceval(uint32_t ip, int count) {
  assert(ip < 0xffffff); assert(count < 256);
  return ((uint32_t)count<<24) | ip;
}

static int traceval_type_to_count(uint8_t type) {
  switch(type) {
  case VM_TYPE_NONE: return 0;
  case VM_TYPE_INT:
  case VM_TYPE_FLOAT:
  case VM_TYPE_STR:
  case VM_TYPE_KEY:
  case VM_TYPE_LIST:
  case VM_TYPE_PTR:
  case VM_TYPE_RET_ADDR:
    return 1;
  case VM_TYPE_VECT:
    return 3;
  case VM_TYPE_ROT:
    return 4;
  default:
    printf("FATAL ERROR: unhandled type %i in traceval_type_to_count\n", type);
    fflush(stdout);
    assert(0); abort(); return 0;
  }

}

static vm_traceval traceval_pop(vm_traceval cur, uint8_t type, 
				script_state *st) {
  int count = traceval_type_to_count(type);

  while(count > 0) {
    int tv_num = TRACEVAL_COUNT(cur);
    uint32_t tv_ip = TRACEVAL_IP(cur);
    count -= tv_num;
    if(count < 0) {
      return build_traceval(tv_ip, -count);
    }
    if(tv_ip == 0 && count == 0)
      return build_traceval(0, 0);
    assert(tv_ip > 0);
    cur = st->tracevals[tv_ip];
  }
  return cur;
}

typedef std::vector<uint8_t> traceval_stack;

static void traceval_to_stack(vm_traceval cur, script_state *st,
			      uint32_t ip, traceval_stack &stack) {
  for(;;) {
    int tv_num = TRACEVAL_COUNT(cur);
    uint32_t tv_ip = TRACEVAL_IP(cur);
    if(tv_ip == 0) break;
    assert(tv_num > 0);

    uint16_t insn = st->bytecode[tv_ip];
    uint8_t vtype;
    switch(GET_ICLASS(insn)) {
    case ICLASS_NORMAL:
      vtype = vm_insns[GET_IVAL(insn)].ret;
      break;
    case ICLASS_RDL_I:
    case ICLASS_RDG_I:
      vtype = VM_TYPE_INT; break;
    case ICLASS_RDL_P:
    case ICLASS_RDG_P:
      vtype = VM_TYPE_PTR; break;
    case ICLASS_WRL_I:
    case ICLASS_WRL_P:
    case ICLASS_WRG_I:
    case ICLASS_WRG_P:
    case ICLASS_JUMP:
    case ICLASS_CALL:
      vtype = VM_TYPE_NONE; break;
    default:
      printf("FATAL ERROR: unhandled iclass %i in traceval_to_stack\n",
	     (int)GET_ICLASS(insn));
      abort(); return;
    }

    assert(vtype != VM_TYPE_NONE);
    if(vtype == VM_TYPE_VECT || vtype == VM_TYPE_ROT)
      vtype = VM_TYPE_FLOAT;
    for(int i = 0; i < tv_num; i++)
      stack.push_back(vtype);

    cur = st->tracevals[tv_ip];
  }

  vm_function *func = NULL;
  for(int i = 0; i < st->num_funcs; i++) {
    if(st->funcs[i].insn_ptr == 0) continue;
    if(st->funcs[i].insn_ptr <= ip && 
       ip < st->funcs[i].insn_end) {
      func = &st->funcs[i]; 
    }
  }
  assert(func != NULL);

  traceval_stack args;
  int count = TRACEVAL_COUNT(cur);
#if 0
  for(uint8_t *arg_type = func->arg_types+func->arg_count-1;
      arg_type >= func->arg_types && count > 0; arg_type--) {
#else
    for(int argno = 0; argno < func->arg_count && count > 0; argno++) {
      uint8_t *arg_type = func->arg_types+argno;
#endif
    switch(*arg_type) {
    case VM_TYPE_INT:
    case VM_TYPE_FLOAT:
    case VM_TYPE_STR:
    case VM_TYPE_KEY:
    case VM_TYPE_LIST:
      args.push_back(*arg_type); count--;
      break;
    case VM_TYPE_ROT:
      if(count > 0) { args.push_back(VM_TYPE_FLOAT); count--; }
      /* fall through */
    case VM_TYPE_VECT:
      if(count > 0) { args.push_back(VM_TYPE_FLOAT); count--; }
      if(count > 0) { args.push_back(VM_TYPE_FLOAT); count--; }
      if(count > 0) { args.push_back(VM_TYPE_FLOAT); count--; }
      break;
    default:
      printf("FATAL ERROR: unhandled arg type %i in traceval_to_stack\n",
	     *arg_type);
      abort(); break;
    }
  }
  if(count > 0) {
    printf("FATAL ERROR: excess args %i in traceval_to_stack\n",
	   count);
    abort();
  }
  while(!args.empty()) {
    stack.push_back(args.back()); args.pop_back();
  }
}

static void calc_stack_curop(traceval_stack &stack, uint8_t vtype) {
  switch(vtype) {
  case VM_TYPE_NONE: 
    break;
  case VM_TYPE_INT:
  case VM_TYPE_FLOAT:
  case VM_TYPE_KEY:
  case VM_TYPE_STR:
  case VM_TYPE_LIST:
  case VM_TYPE_RET_ADDR:
    stack.push_back(vtype); break;
  case VM_TYPE_ROT:
    stack.push_back(VM_TYPE_FLOAT);
    /* fall through */
  case VM_TYPE_VECT:
    stack.push_back(VM_TYPE_FLOAT);
    stack.push_back(VM_TYPE_FLOAT);
    stack.push_back(VM_TYPE_FLOAT);
    break;
  default:
    printf("FATAL: unhandled type %i in  calc_stack_curop\n", vtype);
    fflush(stdout); abort();
  }
}

static void script_calc_stack(script_state *st, traceval_stack &stack) {
  uint16_t insn = st->bytecode[st->ip];
  switch(GET_ICLASS(insn)) {
  case ICLASS_NORMAL:
    calc_stack_curop(stack, vm_insns[GET_IVAL(insn)].arg2);
    calc_stack_curop(stack, vm_insns[GET_IVAL(insn)].arg1);
    break;
  case ICLASS_RDL_I:
  case ICLASS_RDG_I:
  case ICLASS_RDL_P:
  case ICLASS_RDG_P:
    break;
  case ICLASS_WRL_I:
  case ICLASS_WRG_I:
    stack.push_back(VM_TYPE_INT); break;
  case ICLASS_WRL_P:
  case ICLASS_WRG_P:
    stack.push_back(VM_TYPE_PTR); break;
  case ICLASS_JUMP:
    break;
  case ICLASS_CALL:
    {
      uint16_t ival = GET_IVAL(insn);
      assert(ival < st->num_funcs);
      vm_function *func = &st->funcs[ival];
      for(int i = func->arg_count - 1; i >= 0; i--) {
	calc_stack_curop(stack, func->arg_types[i]);
      }
      calc_stack_curop(stack, VM_TYPE_RET_ADDR);
      break;
    }
  default:
    printf("FATAL ERROR: unhandled iclass %i in traceval_to_stack\n",
	   (int)GET_ICLASS(insn));
    abort(); return;
  }

  traceval_to_stack(st->tracevals[st->ip], st, st->ip, stack);
}

static int verify_pass2(unsigned char * visited, uint16_t *bytecode, 
			vm_function *func, script_state *st) {
  std::vector<pass2_state> pending; const char* err = NULL;
  std::map<uint32_t,asm_verify*> done;
  {
    int arg_size_tv = 0;
    for(int i = 0; i < func->arg_count; i++) {
      arg_size_tv += traceval_type_to_count(func->arg_types[i]);
    }
    if(arg_size_tv > 255) {
      err = "Arguments too big for traceval code"; goto out;
    }

    asm_verify* verify = new asm_verify(err, func, ptr_stack_sz());
    pending.push_back(pass2_state(func->insn_ptr, 
				  build_traceval(0, arg_size_tv), verify));
  }
  
  func->max_stack_use = 0;

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
	  if(vs.verify->get_max_stack() > func->max_stack_use)
	    func->max_stack_use = vs.verify->get_max_stack();

	  vs.verify->combine_verify(iter->second); delete vs.verify;
	  if(err != NULL) goto out; else goto next_chunk;
	}
      }


      uint16_t insn = bytecode[vs.ip];
#ifdef DEBUG_TRACEVALS
      printf("DEBUG: verifying 0x%04x @ %i, trace=0x%lx\n",
	     (unsigned)insn, (int)vs.ip, (unsigned long)vs.trace);
#endif
      uint32_t next_ip = vs.ip+1;
      
      switch(GET_ICLASS(insn)) {
      case ICLASS_NORMAL:
	{
	  uint16_t ival = GET_IVAL(insn);
	  assert(ival < NUM_INSNS); // checked pass 1

	  insn_info info = vm_insns[ival];
	  vs.verify->pop_val(info.arg2);
	  vs.verify->pop_val(info.arg1);
	  vs.verify->push_val(info.ret);
	  if(err != NULL) { delete vs.verify; goto out; }

	  vs.trace = traceval_pop(vs.trace, info.arg2, st);
#ifdef DEBUG_TRACEVALS
	  printf("DEBUG: popped type %i, new trace 0x%lx\n",
		 info.arg2, (unsigned long)vs.trace);
#endif
	  vs.trace = traceval_pop(vs.trace, info.arg1, st);
#ifdef DEBUG_TRACEVALS
	  printf("DEBUG: popped type %i, new trace 0x%lx\n",
		 info.arg1, (unsigned long)vs.trace);
#endif
	  st->tracevals[vs.ip] = vs.trace;
	  if(info.ret != VM_TYPE_NONE) {
	    int tv_count = traceval_type_to_count(info.ret);
	    assert(tv_count > 0);
	    vs.trace = build_traceval(vs.ip, tv_count);
	  }

#ifdef DEBUG_TRACEVALS
	  {
	    traceval_stack tv_stack;
	    traceval_to_stack(vs.trace, st, vs.ip, tv_stack);
	    printf("DEBUG: traceval stack:");
	    for(std::vector<uint8_t>::iterator tv_iter = tv_stack.end();
		tv_iter != tv_stack.begin(); /* */) {
	      tv_iter--;
	      printf(" %i", (int) *tv_iter);
	    }
	    printf("\n");

	    std::vector<uint8_t>::iterator vs_iter = 
	      vs.verify->stack_types.end();
	    for(std::vector<uint8_t>::iterator tv_iter = tv_stack.begin();
		tv_iter != tv_stack.end(); tv_iter++) {
	      assert(vs_iter != vs.verify->stack_types.begin());
	      vs_iter--;
	      if(caj_vm_check_types(*tv_iter, *vs_iter)) {
		printf("INTERNAL ERROR: traceval/verify stack mismatch %i %i\n",
		       (int)*tv_iter, (int)*vs_iter);
		vs.verify->dump_stack("verify ");
		fflush(stdout); abort();
	      }
	    }

	    tv_stack.clear();
	  }
#endif

	  switch(info.special) {
	  case IVERIFY_INVALID: 
	    assert(0); break; // checked pass 1
	  case IVERIFY_RET:
	    if(vs.verify->get_max_stack() > func->max_stack_use)
	      func->max_stack_use = vs.verify->get_max_stack();

	    delete vs.verify; goto next_chunk;
	  case IVERIFY_COND:
	    // execution could skip the next instruction...
	    pending.push_back(pass2_state(next_ip+1, vs.trace, vs.verify->dup()));
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
	  st->tracevals[vs.ip] = vs.trace;
	  vs.trace = build_traceval(vs.ip, 1);
	  if(fudge + ival >= VM_MAX_IVAL) {
	    err = "64-bit fudge exceeds max ival. Try on a 32-bit VM?";
	    delete vs.verify; goto out;
	  };
	  if(fudge > 0) {
	    assert(st->patched_bytecode != NULL);
	    st->patched_bytecode[vs.ip] = bytecode[vs.ip] + fudge;
	  }
	  break;
	}
      case ICLASS_WRL_I:
	{
	  int16_t ival = GET_IVAL(insn);
	  int fudge = vs.verify->check_wrl_i(ival)*(ptr_stack_sz()-1);
	  if(err != NULL) { delete vs.verify; goto out; }
	  vs.trace = traceval_pop(vs.trace, VM_TYPE_INT, st);
	  st->tracevals[vs.ip] = vs.trace;
	  if(fudge + ival >= VM_MAX_IVAL) {
	    err = "64-bit fudge exceeds max ival. Try on a 32-bit VM?";
	    delete vs.verify; goto out;
	  };
	  if(fudge > 0) {
	    assert(st->patched_bytecode != NULL);
	    st->patched_bytecode[vs.ip] = bytecode[vs.ip] + fudge;
	  }
	  break;
	}
      case ICLASS_RDL_P:
	{
	  int16_t ival = GET_IVAL(insn);
	  int fudge = vs.verify->check_rdl_p(ival)*(ptr_stack_sz()-1);
	  if(err != NULL) { delete vs.verify; goto out; }
	  st->tracevals[vs.ip] = vs.trace;
	  vs.trace = build_traceval(vs.ip, 1);
	  if(fudge + ival >= VM_MAX_IVAL) {
	    err = "64-bit fudge exceeds max ival. Try on a 32-bit VM?";
	    delete vs.verify; goto out;
	  };
	  if(fudge > 0) {
	    assert(st->patched_bytecode != NULL);
	    st->patched_bytecode[vs.ip] = bytecode[vs.ip] + fudge;
	  }
	  break;
	}
      case ICLASS_WRL_P:
	{
	  int16_t ival = GET_IVAL(insn);
	  int fudge = vs.verify->check_wrl_p(ival)*(ptr_stack_sz()-1);
	  if(err != NULL) { delete vs.verify; goto out; }
	  vs.trace = traceval_pop(vs.trace, VM_TYPE_PTR, st);
	  st->tracevals[vs.ip] = vs.trace;
	  if(fudge + ival >= VM_MAX_IVAL) {
	    err = "64-bit fudge exceeds max ival. Try on a 32-bit VM?";
	    delete vs.verify; goto out;
	  };
	  if(fudge > 0) {
	    assert(st->patched_bytecode != NULL);
	    st->patched_bytecode[vs.ip] = bytecode[vs.ip] + fudge;
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
	  if(err != NULL) { delete vs.verify; goto out; };
	  st->tracevals[vs.ip] = vs.trace;
	  vs.trace = build_traceval(vs.ip, 1);
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
	  if(err != NULL) { delete vs.verify; goto out; };
	  vs.trace = traceval_pop(vs.trace, VM_TYPE_INT, st);
	  st->tracevals[vs.ip] = vs.trace;
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
	  if(err != NULL) { delete vs.verify; goto out; };
	  st->tracevals[vs.ip] = vs.trace;
	  vs.trace = build_traceval(vs.ip, 1);
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
	  if(err != NULL) { delete vs.verify; goto out; };
	  vs.trace = traceval_pop(vs.trace, VM_TYPE_PTR, st);
	  st->tracevals[vs.ip] = vs.trace;
	  break;
	}	
      case ICLASS_JUMP:
	{
	  st->tracevals[vs.ip] = vs.trace;
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

	  if(err != NULL) { delete vs.verify; goto out; };

	  for(int i = func->arg_count - 1; i >= 0; i--)
	    vs.trace = traceval_pop(vs.trace,func->arg_types[i] , st);
	  vs.trace = traceval_pop(vs.trace, VM_TYPE_RET_ADDR, st);
	  st->tracevals[vs.ip] = vs.trace;
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

  if(st->bytecode_len <= 0 || st->bytecode[0] != INSN_QUIT) {
    printf("SCRIPT VERIFY ERR: bytecode at 0 must be QUIT\n");
    return 0;
  }

  if(ptr_stack_sz() != 1) {
    st->patched_bytecode = new uint16_t[st->bytecode_len];
    memcpy(st->patched_bytecode, st->bytecode, 
	   st->bytecode_len*sizeof(uint16_t));
  }

  st->tracevals = new vm_traceval[st->bytecode_len];

  unsigned char *visited = new uint8_t[st->bytecode_len];
  memset(visited, 0, st->bytecode_len);
  for(int i = 0; i < st->num_funcs; i++) {
    if(st->funcs[i].insn_ptr == 0) continue;
    if(!verify_pass1(visited, st->bytecode, &st->funcs[i])) goto out_fail;
    if(!verify_pass2(visited, st->bytecode, &st->funcs[i], st)) goto out_fail;
  }

  if(st->patched_bytecode == NULL)
    st->patched_bytecode = st->bytecode;

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

static void unwind_stack(script_state * st) {
  traceval_stack stack;
  while(st->ip != 0) {
    stack.clear();
    script_calc_stack(st, stack);
    for(traceval_stack::iterator iter = stack.begin(); 
	iter != stack.end(); iter++) {
      switch(*iter) {
      case VM_TYPE_INT:
      case VM_TYPE_FLOAT:
      case VM_TYPE_RET_ADDR:
	st->stack_top++; break;
      case VM_TYPE_STR:
      case VM_TYPE_KEY:
      case VM_TYPE_LIST:
      case VM_TYPE_PTR:
	{
	  heap_header *p = get_stk_ptr(st->stack_top+1);
	  if(p != NULL) heap_ref_decr(p, st); 
	  st->stack_top += ptr_stack_sz();
	  break;
	}
      default:
	printf("FATAL: unhandled type %i in unwind_stack\n", (int)*iter);
	fflush(stdout); abort(); return;
      }
    }

    st->ip = *(++st->stack_top); 
    printf("DEBUG: unwind_stack: new ip = 0x%x\n", (unsigned)st->ip);
  }
}

static heap_header *list_2_str(heap_header* list, int32_t pos, script_state *st) {
  heap_header* ret;
  if(pos < 0 || (uint32_t)pos >= list->len) {
    ret = script_alloc(st, 0, VM_TYPE_STR);
  } else {
    heap_header **items = (heap_header**)script_getptr(list);
    heap_header *item = items[pos];
    switch(heap_entry_vtype(item)) {
    case VM_TYPE_STR:
    case VM_TYPE_KEY:
      heap_ref_incr(item); ret = item; break;
    default:
      ret = script_alloc(st, 0, VM_TYPE_STR); break;
    }
  }
  heap_ref_decr(list, st); return ret;
}

static heap_header *cast_list2s(heap_header* list, script_state *st) {
  heap_header **items = (heap_header**)script_getptr(list);
  uint32_t count = list->len; char buffer[60];
  std::string str;
  for(uint32_t i = 0; i < count; i++) {
    heap_header *item = items[i];
    switch(heap_entry_vtype(item)) {
    case VM_TYPE_STR:
    case VM_TYPE_KEY:
      str.append((char*)script_getptr(item), item->len);
      break;
    case VM_TYPE_INT:
      snprintf(buffer, 60, "%i", (int)*(int32_t*)script_getptr(item));
      str.append(buffer);
      break;
    case VM_TYPE_FLOAT:
      snprintf(buffer, 60, "%f", *(float*)script_getptr(item));
      str.append(buffer);
      break;
    case VM_TYPE_VECT:
      {
	float* vect = (float*)script_getptr(item);
	snprintf(buffer, 60, "<%.6f, %.6f, %.6f>",
		 vect[0], vect[1], vect[2]);
	str.append(buffer);
	break;
      }
    case VM_TYPE_ROT:
      {
	float* vect = (float*)script_getptr(item);
	snprintf(buffer, 60, "<%.6f, %.6f, %.6f, %.6f>",
		 vect[0], vect[1], vect[2], vect[3]);
	str.append(buffer);
	break;
      }
    default:
      str.append("[UNHANDLED]");
      break;
    }
  }
  heap_ref_decr(list, st);
  
  return make_vm_string(st, str);
}

static void step_script(script_state* st, int num_steps) {
  uint16_t* bytecode = st->patched_bytecode;
  int32_t* stack_top = st->stack_top;
  uint32_t ip = st->ip;
  assert((st->ip & 0x80000000) == 0 && st->scram_flag == 0); // caller should know better :-P
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
	if(unlikely(stack_top[1] == 0)) { 
	  stack_top++; st->scram_flag = VM_SCRAM_DIV_ZERO; goto abort_exec;
	}
	if(unlikely(stack_top[1] == -1 && stack_top[2] == -2147483648)) {
	  // evil hack. This division causes an FPE!
	  stack_top[2] = -2147483648;
	} else {
	  stack_top[2] = stack_top[2] / stack_top[1];
	}
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
	if(unlikely(stack_top[1] == 0)) {
	  st->scram_flag = VM_SCRAM_DIV_ZERO; 
	  goto abort_exec;
	}
	if(unlikely(stack_top[1] == -1 && stack_top[2] == -2147483648)) {
	  // evil hack. This division causes an FPE!
	  stack_top[2] = 0;
	} else {
	  stack_top[2] = stack_top[2] % stack_top[1];
	}
	stack_top++;
	break;
      case INSN_AND_II:
	stack_top[2] = stack_top[2] & stack_top[1];
	stack_top++;
	break;
      case INSN_OR_II:
	stack_top[2] = stack_top[2] | stack_top[1];
	stack_top++;
	break;
      case INSN_XOR_II:
	stack_top[2] = stack_top[2] ^ stack_top[1];
	stack_top++;
	break;
      case INSN_NOT_I:
	stack_top[1] = ~stack_top[1];
	break;	
      // TODO - implement << and >> operations
      case INSN_SHR:
	stack_top[2] = stack_top[2] >> stack_top[1];
	stack_top++;
	break;
      case INSN_SHL:
	stack_top[2] = stack_top[2] << stack_top[1];
	stack_top++;
	break;
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
      case INSN_DROP_P:
	heap_ref_decr(get_stk_ptr(stack_top+1), st); 
	stack_top += ptr_stack_sz();
	break;
      case INSN_DROP_I3:
	stack_top += 3; break;
      case INSN_DROP_I4:
	stack_top += 4; break;
      case INSN_QUIT: // dirty dirty hack... that isn't actually used?
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
      case INSN_CAST_I2S:
	{
	  char buf[40]; sprintf(buf, "%i", (int)*(++stack_top));
	  stack_top -= ptr_stack_sz();
	  heap_header *p = make_vm_string(st, buf);;  
	  put_stk_ptr(stack_top+1,p);
	  if(p == NULL) goto abort_exec;
	  break;
	}
      case INSN_CAST_F2S:
	{
	  char buf[40]; sprintf(buf, "%f", *(float*)(++stack_top));
	  stack_top -= ptr_stack_sz();
	  heap_header *p = make_vm_string(st, buf);  
	  put_stk_ptr(stack_top+1,p);
	  if(p == NULL) goto abort_exec;
	  break;
	}
	/* FIXME - implement other casts */
      case INSN_BEGIN_CALL:
	// --stack_top; // the magic is in the verifier.
	*(stack_top--) = 0x1231234; // for debugging
	break;
      case INSN_INC_I:
	stack_top[1]++; break;
      case INSN_DEC_I:
	stack_top[1]--; break;
      case INSN_ADD_SS:
	{
	  heap_header *p2 = get_stk_ptr(stack_top+1); 
	  heap_header *p1 = get_stk_ptr(stack_top+1+ptr_stack_sz()); 
	  stack_top += ptr_stack_sz();
	  heap_header *pnew = script_alloc(st, p1->len+p2->len, VM_TYPE_STR);
	  if(pnew != NULL) {
	    memcpy(script_getptr(pnew), script_getptr(p1), p1->len);
	    memcpy((char*)script_getptr(pnew)+p1->len, script_getptr(p2), p2->len);
	  }
	  heap_ref_decr(p1,st); heap_ref_decr(p2,st); 
	  put_stk_ptr(stack_top+1,pnew);
	  if(pnew == NULL) goto abort_exec; 
	  break;
	}
      case INSN_EQ_FF:
	stack_top[2] = ((float*)stack_top)[2] == ((float*)stack_top)[1];
	stack_top++;
	break;
      case INSN_NEQ_FF:
	stack_top[2] = ((float*)stack_top)[2] != ((float*)stack_top)[1];
	stack_top++;
	break;
      case INSN_GR_FF:
	stack_top[2] = ((float*)stack_top)[2] > ((float*)stack_top)[1];
	stack_top++;
	break;
      case INSN_LES_FF:
	stack_top[2] = ((float*)stack_top)[2] < ((float*)stack_top)[1];
	stack_top++;
	break;
      case INSN_GEQ_FF:
	stack_top[2] = ((float*)stack_top)[2] >= ((float*)stack_top)[1];
	stack_top++;
	break;
      case INSN_LEQ_FF:
	stack_top[2] = ((float*)stack_top)[2] <= ((float*)stack_top)[1];
	stack_top++;
	break;
      case INSN_EQ_SS:
      case INSN_EQ_KK:
	{
	  heap_header *p2 = get_stk_ptr(stack_top+1); 
	  heap_header *p1 = get_stk_ptr(stack_top+1+ptr_stack_sz()); 
	  stack_top += ptr_stack_sz()*2 - 1;
	  stack_top[1] = (p1->len == p2->len && 
			  strncmp((char*)script_getptr(p1), 
				  (char*)script_getptr(p2), p1->len) == 0);
	  heap_ref_decr(p1,st); heap_ref_decr(p2,st); 
	  break;
	}
      case INSN_CAST_F2B:
	stack_top[1] = (0.0f != *(float*)(stack_top+1));
	break;
      case INSN_CAST_S2B:
	{
	  heap_header *p = get_stk_ptr(stack_top+1);
	  stack_top += ptr_stack_sz() - 1;
	  stack_top[1] = (p->len != 0);
	  heap_ref_decr(p, st); 
	  break;
	}
      case INSN_CAST_K2B:
	{
	  // FIXME - could do with a more efficient implementation
	  uuid_t u; char buf[40];
	  heap_header *p = get_stk_ptr(stack_top+1);
	  stack_top += ptr_stack_sz() - 1;
	  if(p->len == 36) {
	    strncpy(buf, (char*)script_getptr(p), 36); buf[36] = 0;
	    stack_top[1] = (uuid_parse(buf, u) == 0 && !uuid_is_null(u));
	  } else {
	    stack_top[1] = 0;
	  }
	  heap_ref_decr(p, st); 
	  break;
	}
      case INSN_CAST_L2B: // FIXME - merge code with CAST_S2B above?
	{
	   heap_header *p = get_stk_ptr(stack_top+1);
	   stack_top += ptr_stack_sz() - 1;
	   stack_top[1] = (p->len != 0);
	   heap_ref_decr(p, st); 
	   break;
	}
      case INSN_CAST_V2B:
	{
	  int truth = *(float*)(stack_top+1) != 0.0f ||
	    *(float*)(stack_top+2) != 0.0f || *(float*)(stack_top+3) != 0.0f;
	  stack_top += 2; stack_top[1] = truth;
	  break;
	}
      case INSN_CAST_R2B:
	{
	  int truth = *(float*)(stack_top+1) != 0.0f ||
	    *(float*)(stack_top+2) != 0.0f || *(float*)(stack_top+3) != 0.0f ||
	    *(float*)(stack_top+4) != 1.0f ;
	  stack_top += 3; stack_top[1] = truth;
	  break;
	}
      // FIXME - implement the rest of the CAST_?2B insns
      case INSN_ABS:
	stack_top[1] = abs(stack_top[1]); // use labs?
	break;
      case INSN_FABS:
	((float*)stack_top)[1] = fabsf(((float*)stack_top)[1]);
	break;
      case INSN_SQRT:
	// FIXME - should check if this is negative (then NaN is returned);
	((float*)stack_top)[1] = sqrtf(((float*)stack_top)[1]);
	break;
      // case INSN_POW: todo!
      case INSN_SIN:
	((float*)stack_top)[1] = sinf(((float*)stack_top)[1]);
	break;
      case INSN_COS:
	((float*)stack_top)[1] = cosf(((float*)stack_top)[1]);
	break;
      case INSN_TAN:
	((float*)stack_top)[1] = tanf(((float*)stack_top)[1]);
	break;
      case INSN_ASIN:
	((float*)stack_top)[1] = asinf(((float*)stack_top)[1]);
	break;
      case INSN_ACOS:
	((float*)stack_top)[1] = acosf(((float*)stack_top)[1]);
	break;
      case INSN_ATAN:
	((float*)stack_top)[1] = atanf(((float*)stack_top)[1]);
	break;
	// case INSN_ATAN2: todo!
      case INSN_CAST_V2S:
	{
	  char buf[60]; 
	  snprintf(buf, 60, "<%.5f, %.5f, %.5f>",
		   *(float*)(stack_top+1), *(float*)(stack_top+2),
		   *(float*)(stack_top+3));
	  stack_top += 3;
	  stack_top -= ptr_stack_sz();
	  heap_header *p = make_vm_string(st, buf);  
	  put_stk_ptr(stack_top+1,p);
	  if(p == NULL) goto abort_exec;
	  break;
	}
      case INSN_CAST_R2S:
	{
	  char buf[60]; 
	  snprintf(buf, 60, "<%.5f, %.5f, %.5f, %.5f>",
		   *(float*)(stack_top+1), *(float*)(stack_top+2),
		   *(float*)(stack_top+3), *(float*)(stack_top+4));
	  stack_top += 4;
	  stack_top -= ptr_stack_sz();
	  heap_header *p = make_vm_string(st, buf);  
	  put_stk_ptr(stack_top+1,p);
	  if(p == NULL) goto abort_exec;
	  break;
	}
	// FIXME - TODO - bunch of vector ops
      case INSN_ADD_VV:
	*(float*)(stack_top+4) +=  *(float*)(stack_top+1);
	*(float*)(stack_top+5) +=  *(float*)(stack_top+2);
	*(float*)(stack_top+6) +=  *(float*)(stack_top+3);
	stack_top +=3;
	break;
      case INSN_SUB_VV:
	*(float*)(stack_top+4) -=  *(float*)(stack_top+1);
	*(float*)(stack_top+5) -=  *(float*)(stack_top+2);
	*(float*)(stack_top+6) -=  *(float*)(stack_top+3);
	stack_top +=3;
	break;
      case INSN_DOT_VV:
	*(float*)(stack_top+6) = *(float*)(stack_top+4) * *(float*)(stack_top+1)
	  + *(float*)(stack_top+5) * *(float*)(stack_top+2)
	  + *(float*)(stack_top+6) * *(float*)(stack_top+3);
	stack_top += 5;
	break;
      case INSN_CROSS_VV:
	{
	  // again, a bit of a hack...
	  caj_cross_vect3((caj_vector3*)(stack_top+4),
			  (caj_vector3*)(stack_top+4),
			  (caj_vector3*)(stack_top+1));
	  stack_top += 3;
	  break;
	}
      case INSN_MUL_VF:
	// okay, this is easy since the vector is pushed first
	*(float*)(stack_top+2) *= *(float*)(stack_top+1);
	*(float*)(stack_top+3) *= *(float*)(stack_top+1);
	*(float*)(stack_top+4) *= *(float*)(stack_top+1);
	stack_top++;
	break;
      case INSN_MUL_FV:
	{
	  // harder - stuff is in the wrong order.
	  float f = *(float*)(stack_top+4);
	  *(float*)(stack_top+4) = *(float*)(stack_top+3) * f;
	  *(float*)(stack_top+3) = *(float*)(stack_top+2) * f;
	  *(float*)(stack_top+2) = *(float*)(stack_top+1) * f;
	  stack_top++;
	  break;
	}
      case INSN_DIV_VF:
	*(float*)(stack_top+2) /= *(float*)(stack_top+1);
	*(float*)(stack_top+3) /= *(float*)(stack_top+1);
	*(float*)(stack_top+4) /= *(float*)(stack_top+1);
	stack_top++;
	break;
      case INSN_MUL_VR:
	{
	  // horrid horrid HACK - FIXME!
	  caj_mult_vect3_quat((caj_vector3*)(stack_top+5),
			      (caj_quat*)(stack_top+1),
			      (caj_vector3*)(stack_top+5));
	  stack_top += 4;
	  break;
	}
      case INSN_DIV_VR:
	{
	  // horrid horrid HACK - FIXME!
	  caj_quat *rhs = (caj_quat*)(stack_top+1);
	  rhs->x = -rhs->x; rhs->y = -rhs->y; rhs->z = -rhs->z; 
	  caj_mult_vect3_quat((caj_vector3*)(stack_top+5),
			      rhs,
			      (caj_vector3*)(stack_top+5));
	  stack_top += 4;
	  break;
	}	
      case INSN_ADD_RR:
	*(float*)(stack_top+5) +=  *(float*)(stack_top+1);
	*(float*)(stack_top+6) +=  *(float*)(stack_top+2);
	*(float*)(stack_top+7) +=  *(float*)(stack_top+3);
	*(float*)(stack_top+8) +=  *(float*)(stack_top+4);
	stack_top += 4;
	break;
      case INSN_SUB_RR:
	*(float*)(stack_top+5) -=  *(float*)(stack_top+1);
	*(float*)(stack_top+6) -=  *(float*)(stack_top+2);
	*(float*)(stack_top+7) -=  *(float*)(stack_top+3);
	*(float*)(stack_top+8) -=  *(float*)(stack_top+4);
	stack_top += 4;
	break;
      case INSN_MUL_RR:
	{ 
	  // again, a bit of a hack...
	  caj_mult_quat_quat((caj_quat*)(stack_top+5),
			      (caj_quat*)(stack_top+1),
			      (caj_quat*)(stack_top+5));
	  stack_top += 4;
	}
	break;
      case INSN_DIV_RR:
	{ 
	  // again, a bit of a hack...
	  caj_quat *rhs = (caj_quat*)(stack_top+1);
	  rhs->x = -rhs->x; rhs->y = -rhs->y; rhs->z = -rhs->z; 
	  caj_mult_quat_quat((caj_quat*)(stack_top+5),
			     rhs,
			     (caj_quat*)(stack_top+5));
	  stack_top += 4;
	}
	break;	
      case INSN_NEG_I:
	stack_top[1] = -stack_top[1];
	break;
      case INSN_NEG_F:
	*(float*)(stack_top+1) = - *(float*)(stack_top+1);
	break;
      case INSN_NEG_V:
	*(float*)(stack_top+1) = - *(float*)(stack_top+1);
	*(float*)(stack_top+2) = - *(float*)(stack_top+2);
	*(float*)(stack_top+3) = - *(float*)(stack_top+3);
	break;
      case INSN_NEG_R:
	*(float*)(stack_top+1) = - *(float*)(stack_top+1);
	*(float*)(stack_top+2) = - *(float*)(stack_top+2);
	*(float*)(stack_top+3) = - *(float*)(stack_top+3);
	*(float*)(stack_top+4) = - *(float*)(stack_top+4);
	break;
      case INSN_STRLEN:
	{
	  heap_header *p = get_stk_ptr(stack_top+1);
	  stack_top += ptr_stack_sz()-1;
	  stack_top[1] = p->len; heap_ref_decr(p, st); 
	  break;
	}
      case INSN_LISTLEN:
	{
	  heap_header *p = get_stk_ptr(stack_top+1);
	  stack_top += ptr_stack_sz()-1;
	  stack_top[1] = p->len; heap_ref_decr(p, st);
	  break;
	}
	// FIXME - TODO rest of list instructions
      case INSN_LIST2STR:
	{
	  int32_t off = stack_top[1]; stack_top++;
	  put_stk_ptr(stack_top+1,list_2_str(get_stk_ptr(stack_top+1), off, st));
	  if(st->scram_flag != 0) goto abort_exec;
	  break;
	}
      case INSN_CAST_LIST2S:
	{
	   heap_header *list = get_stk_ptr(stack_top+1);
	   heap_header *str = cast_list2s(list, st);
	   put_stk_ptr(stack_top+1, str);
	   // cast_list2s handles refcounting.
	   if(list == NULL) goto abort_exec;
	   break;
	}
      case INSN_CAST_I2L:
	{
	  heap_header *p = make_num_on_heap(st, stack_top+1, VM_TYPE_INT, 1);
	  heap_header *list = make_single_list(st, p);
	  stack_top -= ptr_stack_sz() - 1;
	  put_stk_ptr(stack_top+1, list);
	  if(list == NULL) goto abort_exec;
	  break;
	}
      case INSN_CAST_F2L:
	{
	  heap_header *p = make_num_on_heap(st, stack_top+1, VM_TYPE_FLOAT, 1);
	  heap_header *list = make_single_list(st, p);
	  stack_top -= ptr_stack_sz() - 1;
	  put_stk_ptr(stack_top+1, list);
	  if(list == NULL) goto abort_exec;
	  break;
	}
      case INSN_CAST_V2L:
	{
	  heap_header *p = make_num_on_heap(st, stack_top+1, VM_TYPE_VECT, 3);
	  heap_header *list = make_single_list(st, p);
	  stack_top -= ptr_stack_sz() - 3;
	  put_stk_ptr(stack_top+1, list);
	  if(list == NULL) goto abort_exec;
	  break;
	}
      case INSN_CAST_R2L:
	{
	  heap_header *p = make_num_on_heap(st, stack_top+1, VM_TYPE_ROT, 4);
	  heap_header *list = make_single_list(st, p);
	  stack_top -= ptr_stack_sz() - 4;
	  put_stk_ptr(stack_top+1, list);
	  if(list == NULL) goto abort_exec;
	  break;
	}
      case INSN_CAST_S2L:
      case INSN_CAST_K2L:
	{
	  // FIXME - we need to correct the item type here!

	  heap_header *p = get_stk_ptr(stack_top+1);
	  heap_header *list = make_single_list(st, p);
	  put_stk_ptr(stack_top+1, list);
	  if(list == NULL) {
	    goto abort_exec;
	  }
	  break;
	}
      case INSN_CAST_S2I:
	{
	  heap_header *p = get_stk_ptr(stack_top+1);
	  stack_top += ptr_stack_sz()-1;
	  uint32_t len = p->len; if(len > 59) len = 59;
	  char buffer[60]; 
	  memcpy(buffer, script_getptr(p), len); buffer[len] = 0;
	  stack_top[1] = strtol(buffer, NULL, 0);
	  heap_ref_decr(p, st);
	  break;
	}
      case INSN_CAST_S2F:
	{
	  heap_header *p = get_stk_ptr(stack_top+1);
	  stack_top += ptr_stack_sz()-1;
	  uint32_t len = p->len; if(len > 99) len = 99;
	  char buffer[100]; 
	  memcpy(buffer, script_getptr(p), len); buffer[len] = 0;
	  ((float*)stack_top)[1] = strtof(buffer, NULL);
	  heap_ref_decr(p, st);
	  break;
	}
      case INSN_CAST_S2V:
	{
	  heap_header *p = get_stk_ptr(stack_top+1);
	  stack_top += ptr_stack_sz()-3;
	  uint32_t len = p->len; if(len > 99) len = 99;
	  char buffer[100]; 
	  memcpy(buffer, script_getptr(p), len); buffer[len] = 0;

	  for(int i = 1; i <= 3; i++) 
	    *(float*)(stack_top+i) = 0.0f;
	  sscanf(buffer, "<%f, %f, %f>", (float*)(stack_top+1),
		 (float*)(stack_top+2), (float*)(stack_top+3));
	  heap_ref_decr(p, st);
	  break;
	}	
      case INSN_CAST_S2R:
	{
	  heap_header *p = get_stk_ptr(stack_top+1);
	  stack_top += ptr_stack_sz()-4;
	  uint32_t len = p->len; if(len > 99) len = 99;
	  char buffer[100]; 
	  memcpy(buffer, script_getptr(p), len); buffer[len] = 0;

	  for(int i = 1; i <= 4; i++) 
	    *(float*)(stack_top+i) = 0.0f;
	  sscanf(buffer, "<%f, %f, %f, %f>", (float*)(stack_top+1),
		 (float*)(stack_top+2), (float*)(stack_top+3), 
		 (float*)(stack_top+4));
	  heap_ref_decr(p, st);
	  break;
	}	
      case INSN_ADD_LL:
	{
	  heap_header *p2 = get_stk_ptr(stack_top+1); 
	  heap_header *p1 = get_stk_ptr(stack_top+1+ptr_stack_sz()); 
	  stack_top += ptr_stack_sz();
	  heap_header *pnew = script_alloc_list(st, p1->len+p2->len);
	  if(pnew != NULL) {
	    heap_header **new_items = (heap_header**)script_getptr(pnew);
	    heap_header **items = (heap_header**)script_getptr(p1);
	    for(uint32_t i = 0; i < p1->len; i++) {
	      heap_header *item = items[i];
	      heap_ref_incr(item); 
	      new_items[i] = item;
	    }
	    items = (heap_header**)script_getptr(p2);
	    for(uint32_t i = 0; i < p2->len; i++) {
	      heap_header *item = items[i];
	      heap_ref_incr(item); 
	      new_items[p1->len+i] = item;
	    }
	  }
	  heap_ref_decr(p1,st); heap_ref_decr(p2,st); 
	  put_stk_ptr(stack_top+1,pnew);
	  if(pnew == NULL) goto abort_exec; 
	  break;
	}
	// FIXME - TODO - bunch of vector ops
      default:
	 printf("ERROR: unhandled opcode; insn %i\n",(int)insn);
	 st->scram_flag = VM_SCRAM_BAD_OPCODE; goto abort_exec;
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
	if(ip == 0) {
	  printf("SCRIPT ERROR: unbound native function %s\n", st->funcs[ival].name);
	  st->scram_flag = VM_SCRAM_MISSING_FUNC; goto abort_exec;
	} else if(ip & 0x80000000) {
	  uint32_t func_no = ip & 0x7fffffff;
	  st->stack_top = stack_top; st->ip = ip;
	  st->world->nfuncs[func_no].cb(st, st->priv, func_no);
	  return;
	} else if(st->funcs[ival].max_stack_use > (stack_top - st->stack_start)) { 
	  printf("ERROR: potential stack overflow, aborting\n");
	  st->scram_flag = VM_SCRAM_STACK_OVERFLOW; goto abort_exec;
	}
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
    case ICLASS_WRG_P:
      {
	heap_ref_decr(st->gptrs[GET_IVAL(insn)], st);
	st->gptrs[GET_IVAL(insn)] = get_stk_ptr(stack_top+1);
	stack_top += ptr_stack_sz(); 
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
    case ICLASS_RDL_P:
      {
	heap_header *p = get_stk_ptr(stack_top+GET_IVAL(insn));  
	stack_top -= ptr_stack_sz(); heap_ref_incr(p);
	put_stk_ptr(stack_top+1,p);
	break;
      }
    case ICLASS_WRL_P:
      {
	heap_header *p = get_stk_ptr(stack_top+1);  
	stack_top += ptr_stack_sz();
	heap_ref_decr(get_stk_ptr(stack_top+GET_IVAL(insn)), st);
	put_stk_ptr(stack_top+GET_IVAL(insn),p);
	break;
      }
    default:
      printf("ERROR: unhandled insn class; insn 0x%04x\n",(int)insn);
      st->scram_flag = VM_SCRAM_BAD_OPCODE; goto abort_exec;
    }
  }
 out:
  // note: this code is duplicated in INSN_CALL
  st->stack_top = stack_top; 
  st->ip = ip;
  return; // FIXME;
 abort_exec:
  st->stack_top = stack_top; st->ip = ip;
  printf("DEBUG: aborting code execution\n");
  if(st->scram_flag == 0) st->scram_flag = VM_SCRAM_ERR;
}

int vm_script_is_idle(script_state *st) {
  return st->ip == 0 && st->scram_flag == 0;
}

int vm_script_is_runnable(script_state *st) {
  return st->ip != 0 && st->scram_flag == 0 && (st->ip & 0x80000000) == 0;
}

int vm_script_has_failed(script_state *st) {
  return st->scram_flag != 0;
}

char* vm_script_get_error(script_state *st) {
  switch(st->scram_flag) {
  case VM_SCRAM_OK: return strdup("Script OK? FIXME!");
  case VM_SCRAM_ERR: return strdup("Unspecified script error. FIXME");
  case VM_SCRAM_DIV_ZERO: return strdup("Divide by zero error");
  case VM_SCRAM_STACK_OVERFLOW: return strdup("Stack overflow error");
  case VM_SCRAM_BAD_OPCODE: return strdup("Bad/unimplemented opcode. FIXME");
  case VM_SCRAM_MISSING_FUNC: return strdup("Call to non-existent native func");
  case VM_SCRAM_MEM_LIMIT: return strdup("Memory limit reached");
  default: return strdup("Script error with unknown code. FIXME.");
  }
}

int check_ncall_args(const vm_nfunc_desc &nfunc, const vm_function &func) {
  if(nfunc.ret_type != func.ret_type || nfunc.arg_count != func.arg_count)
    return 1;
  for(int i = 0; i < func.arg_count; i++) {
    if(nfunc.arg_types[i] != func.arg_types[i]) return 1;
  }
  return 0;
}

// the native funcs array is generally global so we don't free it
void vm_prepare_script(script_state *st, void *priv, vm_world *w) {
  assert(st->stack_top == NULL);
  st->stack_start = new int32_t[1024]; // FIXME - pick this properly;
  st->stack_top = st->stack_start+1023;
  st->world = w; st->priv = priv;

  st->cur_state = new uint16_t[w->num_events];
  for(int i = 0; i < w->num_events; i++) st->cur_state[i] = 0xffff;
 
  for(unsigned i = 0; i < st->num_funcs; i++) {
    if(st->funcs[i].insn_ptr == 0) {
      std::map<std::string, int>::iterator iter = 
	w->nfunc_map.find(st->funcs[i].name);
      if(iter != w->nfunc_map.end()) {
	if(check_ncall_args(w->nfuncs[iter->second], st->funcs[i])){
	  printf("ERROR: prototype mismatch binding %s\n",st->funcs[i].name);
	  st->scram_flag = 1; return;
	}
	st->funcs[i].insn_ptr = 0x80000000 | iter->second;
      }
    } else if(st->funcs[i].name[0] == '0' && st->funcs[i].name[1] == ':') {
      // FIXME - need to handle multiple states!

      std::map<std::string, vm_nfunc_desc>::iterator iter = 
	w->event_map.find((st->funcs[i].name+2));
      if(iter != w->event_map.end()) {
	if(check_ncall_args(iter->second, st->funcs[i])){
	  printf("ERROR: prototype mismatch binding %s\n",st->funcs[i].name);
	  st->scram_flag = 1; return;
	}
	st->cur_state[iter->second.number] = i;
      } else {
	printf("WARNING: failed to bind event function %s\n", st->funcs[i].name);
      }
    }
  }
}

void vm_call_event(script_state *st, int event_id, ...) {
  va_list args;

  assert(st->ip == 0 && st->scram_flag == 0);
  assert(st->stack_top != NULL);
  assert(st->cur_state != NULL);

  uint16_t func_no = st->cur_state[event_id];
  if(func_no == 0xffff) {
    printf("DEBUG: no handler for event %i\n", event_id);
    return; // no handler for event
  }

  vm_function *func = &st->funcs[func_no];
 

  switch(func->ret_type) {
  case VM_TYPE_NONE: break;
  case VM_TYPE_INT: // not really needed 
  case VM_TYPE_FLOAT:
    *(st->stack_top--) = 0; break;
  default:
    assert(0); // can't be triggered by scripts, since return type checked
  }
  *(st->stack_top--) = 0; // return pointer

  va_start(args, event_id);
  for(int i = 0; i < func->arg_count; i++) {
    switch(func->arg_types[i]) {
    case VM_TYPE_INT:
      *(st->stack_top--) = va_arg(args, int);
      break;
    case VM_TYPE_FLOAT:
      *(float*)(st->stack_top--) = va_arg(args, double); // promoted from float
      break;
    case VM_TYPE_STR:
    case VM_TYPE_KEY:
      {
	char *s =  va_arg(args, char*);
	int len = strlen(s);
	heap_header* p =  script_alloc(st, len, func->arg_types[i]);
	if(p != NULL) 
	  memcpy(script_getptr(p), s, len);
	st->stack_top -= ptr_stack_sz();
	put_stk_ptr(st->stack_top+1,p);
	break;
      }
    default:
      printf("ERROR: unhandled arg type in vm_call_event\n"); 
      va_end(args); assert(0); abort(); return;
    }
  }
  va_end(args);

  st->ip = func->insn_ptr;
}

int vm_event_has_handler(script_state *st, int event_id) {
  assert(st->cur_state != NULL);
  return st->cur_state[event_id] != 0xffff;
}

void vm_run_script(script_state *st, int num_steps) {
  if(st->scram_flag != 0 || st->ip == 0) return;
  assert(st->stack_top != NULL);
  step_script(st, num_steps);
}

static void llVecNorm_cb(script_state *st, void *sc_priv, int func_id) {
  caj_vector3 v; float mag;
  vm_func_get_args(st, func_id, &v);
  mag = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
  if(finite(mag) && mag > 0.0f) {
    v.x /= mag; v.y /= mag; v.z /= mag;
  }
  vm_func_set_vect_ret(st, func_id, &v);
  vm_func_return(st, func_id);
}

static void llVecMag_cb(script_state *st, void *sc_priv, int func_id) {
  caj_vector3 v; float mag;
  vm_func_get_args(st, func_id, &v);
  mag = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
  vm_func_set_float_ret(st, func_id, mag);
  vm_func_return(st, func_id);
}

// FIXME - TODO - llVecDist

struct vm_world* vm_world_new(void) {
  vm_world *w = new vm_world;
  w->num_events = 0;
  vm_world_add_func(w, "llVecNorm", VM_TYPE_VECT, llVecNorm_cb, 1, VM_TYPE_VECT); 
  vm_world_add_func(w, "llVecMag", VM_TYPE_FLOAT, llVecMag_cb, 1, VM_TYPE_VECT); 
  return w;
}

void vm_world_add_func(vm_world *w, const char* name, uint8_t ret_type, 
		       vm_native_func_cb cb, int arg_count, ...) {
  vm_nfunc_desc desc; va_list vargs;
  desc.cb = cb;
  desc.ret_type = ret_type;
  desc.arg_count = arg_count;
  desc.arg_types = new uint8_t[arg_count];
  va_start(vargs, arg_count);

  for(int i = 0; i < arg_count; i++) {
    desc.arg_types[i] = va_arg(vargs, int);
  }
  va_end(vargs);

  desc.number = w->nfuncs.size(); w->nfuncs.push_back(desc); 
  w->nfunc_map[name] = desc.number;
}

int vm_world_add_event(vm_world *w, const char* name, uint8_t ret_type, 
		       int event_id, int arg_count, ...) {
  vm_nfunc_desc desc; va_list vargs;
  assert(w->num_events == event_id);
  desc.ret_type = ret_type;
  desc.arg_count = arg_count;
  desc.arg_types = new uint8_t[arg_count];

  va_start(vargs, arg_count);
  for(int i = 0; i < arg_count; i++) {
    desc.arg_types[i] = va_arg(vargs, int);
  }
  va_end(vargs);

  desc.number = w->num_events++;
  w->event_map[name] = desc;
  return desc.number;
}

void vm_world_free(vm_world *w) {
  // FIXME - this is incomplete and leaks memory;
  delete w;
}

int32_t vm_list_get_count(heap_header *list) {
  assert(heap_entry_vtype(list) == VM_TYPE_LIST);
  return list->len;
}

uint8_t vm_list_get_type(heap_header *list, int32_t pos) {
  if(pos < 0 || (uint32_t)pos >= list->len) {
    return VM_TYPE_NONE;
  } else {
    heap_header **items = (heap_header**)script_getptr(list);
    heap_header *item = items[pos];
    return heap_entry_vtype(item);
  }
}

char *vm_list_get_str(heap_header *list, int32_t pos) {
  if(pos < 0 || (uint32_t)pos >= list->len) {
    return NULL;
  } else {
    heap_header **items = (heap_header**)script_getptr(list);
    heap_header *item = items[pos];
    if(heap_entry_vtype(item) != VM_TYPE_STR && 
       heap_entry_vtype(item) != VM_TYPE_KEY)
      return NULL;

    int32_t len = item->len;
    char *buf = (char*)malloc(len+1);
    memcpy(buf, script_getptr(item), len);
    buf[len] = 0;
    return buf;
  }
}

int32_t vm_list_get_int(heap_header *list, int32_t pos) {
  if(pos < 0 || (uint32_t)pos >= list->len) {
    return 0;
  } else {
    heap_header **items = (heap_header**)script_getptr(list);
    heap_header *item = items[pos];
    if(heap_entry_vtype(item) != VM_TYPE_INT)
      return 0;

    return *(int32_t*)script_getptr(item);
  }
}

float vm_list_get_float(heap_header *list, int32_t pos) {
  if(pos < 0 || (uint32_t)pos >= list->len) {
    return 0.0f;
  } else {
    heap_header **items = (heap_header**)script_getptr(list);
    heap_header *item = items[pos];
    if(heap_entry_vtype(item) != VM_TYPE_FLOAT)
      return 0.0f;

    return *(float*)script_getptr(item);
  }
}

void vm_list_get_vector(heap_header *list, int32_t pos, caj_vector3* out) {
  if(pos < 0 || (uint32_t)pos >= list->len) {
    out->x = 0.0f; out->y = 0.0f; out->z = 0.0f; return;
  } else {
    heap_header **items = (heap_header**)script_getptr(list);
    heap_header *item = items[pos];
    if(heap_entry_vtype(item) != VM_TYPE_VECT) {
      out->x = 0.0f; out->y = 0.0f; out->z = 0.0f; return;
    }

    float *v = (float*)script_getptr(item);
    out->x = v[0]; out->y = v[1]; out->z = v[2];
  }
}

void vm_func_get_args(script_state *st, int func_no, ...) {
  va_list args;
  vm_nfunc_desc &desc = st->world->nfuncs[func_no];
  assert(st->ip & 0x80000000);

  // FIXME - just use func.frame_size here
  int32_t *frame_ptr = st->stack_top;
  for(int i = 0; i < desc.arg_count; i++)
    frame_ptr += vm_vtype_size(desc.arg_types[i]);



  va_start(args, func_no);
  for(int i = 0; i < desc.arg_count; i++) {
    switch(desc.arg_types[i]) {
    case VM_TYPE_INT:
      *va_arg(args, int*) = *(frame_ptr--);
      break;
    case VM_TYPE_FLOAT:
      *va_arg(args, float*) = *(float*)(frame_ptr--);
      break;
    case VM_TYPE_VECT:
      {
	caj_vector3 * vect = va_arg(args, caj_vector3*);
	vect->z = *(float*)(frame_ptr--);
	vect->y = *(float*)(frame_ptr--);
	vect->x = *(float*)(frame_ptr--);
	break;
      }
    case VM_TYPE_ROT:
      {
	caj_quat * vect = va_arg(args, caj_quat*);
	vect->w = *(float*)(frame_ptr--);
	vect->z = *(float*)(frame_ptr--);
	vect->y = *(float*)(frame_ptr--);
	vect->x = *(float*)(frame_ptr--);
	break;
      }
    case VM_TYPE_STR:
    case VM_TYPE_KEY:
      { 
	frame_ptr -= ptr_stack_sz();
	heap_header *p = get_stk_ptr(frame_ptr+1);
	int32_t len = p->len;
	char *buf = (char*)malloc(len+1);
	memcpy(buf, script_getptr(p), len);
	buf[len] = 0;
	*va_arg(args, char**) = buf;
	break;
      }
    case VM_TYPE_LIST:
      { 
	frame_ptr -= ptr_stack_sz();
	heap_header *p = get_stk_ptr(frame_ptr+1);
	*va_arg(args, heap_header**) = p;
	break;
      }      
    default:
      printf("ERROR: unhandled arg type in vm_func_get_args\n"); 
      va_end(args); return;
    }
  }
  va_end(args);
}

void vm_func_set_int_ret(script_state *st, int func_no, int32_t ret) {
  vm_nfunc_desc &desc = st->world->nfuncs[func_no];
  assert(desc.ret_type == VM_TYPE_INT);

  // FIXME - just use func.frame_size here
  int32_t *frame_ptr = st->stack_top;
  for(int i = 0; i < desc.arg_count; i++)
    frame_ptr += vm_vtype_size(desc.arg_types[i]);

  *(frame_ptr+2) = ret;
}

void vm_func_set_float_ret(script_state *st, int func_no, float ret) {
  vm_nfunc_desc &desc = st->world->nfuncs[func_no];
  assert(desc.ret_type == VM_TYPE_FLOAT);

  // FIXME - just use func.frame_size here
  int32_t *frame_ptr = st->stack_top;
  for(int i = 0; i < desc.arg_count; i++)
    frame_ptr += vm_vtype_size(desc.arg_types[i]);

  *(float*)(frame_ptr+2) = ret;
}

void vm_func_set_str_ret(script_state *st, int func_no, const char* ret) {
  vm_nfunc_desc &desc = st->world->nfuncs[func_no];
  assert(desc.ret_type == VM_TYPE_STR);

  // FIXME - just use func.frame_size here
  int32_t *frame_ptr = st->stack_top;
  for(int i = 0; i < desc.arg_count; i++)
    frame_ptr += vm_vtype_size(desc.arg_types[i]);

  int len = strlen(ret);
  heap_header *p = script_alloc(st, len, VM_TYPE_STR);
  if(p == NULL) return;
  memcpy(script_getptr(p), ret, len);
  // it may make more sense to deref the old val *before* setting the new one,
  // but that requires some monkeying around...
  heap_ref_decr(get_stk_ptr(frame_ptr+2), st);
  put_stk_ptr(frame_ptr+2,p);
}

void vm_func_set_key_ret(script_state *st, int func_no, const uuid_t ret) {
  vm_nfunc_desc &desc = st->world->nfuncs[func_no];
  assert(desc.ret_type == VM_TYPE_KEY);

  // FIXME - just use func.frame_size here
  int32_t *frame_ptr = st->stack_top;
  for(int i = 0; i < desc.arg_count; i++)
    frame_ptr += vm_vtype_size(desc.arg_types[i]);

  
  heap_header *p = script_alloc(st, 36, VM_TYPE_STR);
  if(p == NULL) return;

  // have to use the temporary buffer because CajVM strings not null-terminated
  char buf[37]; uuid_unparse_lower(ret, buf);
  memcpy(script_getptr(p), buf, 36);

  // it may make more sense to deref the old val *before* setting the new one,
  // but that requires some monkeying around...
  heap_ref_decr(get_stk_ptr(frame_ptr+2), st);
  put_stk_ptr(frame_ptr+2,p);
}


void vm_func_set_vect_ret(script_state *st, int func_no, const caj_vector3 *vect) {
  vm_nfunc_desc &desc = st->world->nfuncs[func_no];
  assert(desc.ret_type == VM_TYPE_VECT);

  // FIXME - just use func.frame_size here
  int32_t *frame_ptr = st->stack_top;
  for(int i = 0; i < desc.arg_count; i++)
    frame_ptr += vm_vtype_size(desc.arg_types[i]);

  *(float*)(frame_ptr+2) = vect->x;
  *(float*)(frame_ptr+3) = vect->y;
  *(float*)(frame_ptr+4) = vect->z;

}
void vm_func_set_rot_ret(script_state *st, int func_no, const caj_quat *rot) {
  vm_nfunc_desc &desc = st->world->nfuncs[func_no];
  assert(desc.ret_type == VM_TYPE_ROT);

  // FIXME - just use func.frame_size here
  int32_t *frame_ptr = st->stack_top;
  for(int i = 0; i < desc.arg_count; i++)
    frame_ptr += vm_vtype_size(desc.arg_types[i]);

  *(float*)(frame_ptr+2) = rot->x;
  *(float*)(frame_ptr+3) = rot->y;
  *(float*)(frame_ptr+4) = rot->z;
  *(float*)(frame_ptr+5) = rot->w;
}


void vm_func_return(script_state *st, int func_no) {
  vm_nfunc_desc &desc = st->world->nfuncs[func_no];
  assert(st->ip & 0x80000000);
  for(int i = desc.arg_count-1; i >= 0; i--) {
    switch(desc.arg_types[i]) {
    case VM_TYPE_INT:
    case VM_TYPE_FLOAT:
      st->stack_top++; break;
    case VM_TYPE_VECT:
      st->stack_top += 3; break;
    case VM_TYPE_ROT:
      st->stack_top += 4; break;      
    case VM_TYPE_STR:
    case VM_TYPE_KEY:
    case VM_TYPE_LIST:
      {
	heap_header *p = get_stk_ptr(st->stack_top+1);
	heap_ref_decr(p, st); st->stack_top += ptr_stack_sz();
	break;
      }
    default:
      printf("ERROR: unhandled arg type in vm_func_return\n"); 
      st->scram_flag = 1; return;
    }
  }
  st->ip = *(++st->stack_top); 
}

// FIXME - remove this
void caj_vm_test(script_state *st) {
  int32_t stack[128];
  stack[127] = 0;
  st->stack_top = stack+126;
  st->ip = 1;
  step_script(st, 1000);
}

