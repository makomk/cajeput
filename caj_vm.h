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
#ifndef CAJ_VM_H
#define CAJ_VM_H

#include "caj_types.h"
#include "uuid/uuid.h"

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


// Should we remove this? It's not really needed anymore
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
#define VM_TYPE_PTR 102

/* WARNING WARING - this needs fixing if we increase the number of types */
/* For this reason, do NOT use this for anything that's serialised. Really. */
#define MK_VM_TYPE_PAIR(a, b) (((a) << 3) | b)


// various limits set by the opcode format - FIXME enforce these
#define VM_MAX_GVALS 4095 // max global value variables
#define VM_MAX_GPTRS 4095 // max global pointer vars 
// FIXME - the locals limit is actually on the offset to access them!
#define VM_MAX_LOCALS 2047 // max local vars. Opcodes support more, but issues with 64-bit mode - FIXME
#define VM_MAX_FUNCS 4095 // max functions - FIXME 4096?
#define VM_MAX_IVAL 4095
// below are due to encoding
#define VM_MAX_ARGS 255
#define VM_MAX_FUNC_NAME 255

// artificial limits - FIXME enforce these too
#define VM_LIMIT_HEAP 65536
#define VM_LIMIT_HEAP_ENTRIES (65536/8)
#define VM_LIMIT_INSNS 16384 // too low? Too high?

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


#include <stdlib.h>
#include <cassert>

struct vm_function {
  char* name;
  uint8_t ret_type;
  uint16_t func_num; // only used by vm_asm
  uint32_t insn_ptr;
  uint32_t insn_end; // for verifier
  int arg_count;
  uint8_t* arg_types;
  uint16_t* arg_offsets; // only used by vm_asm
  int frame_sz; // only used by VM
  int max_stack_use; // only used by VM & verifier
};

struct script_state;
struct vm_world;
struct heap_header;
struct caj_logger;

typedef void(*vm_native_func_cb)(script_state *st, void *sc_priv, int func_id);

// so we can call state_entry, update event masks etc on state change.
typedef void(*vm_state_change_cb)(script_state *st, void *sc_priv);

struct vm_world* vm_world_new(vm_state_change_cb state_change_cb);
void vm_world_add_func(vm_world *w, const char* name, uint8_t ret_type, 
		       vm_native_func_cb cb, int arg_count, ...);
int vm_world_add_event(vm_world *w, const char* name, uint8_t ret_type, 
		       int event_id, int arg_count, ...);
void vm_world_free(vm_world *w);

script_state* vm_load_script(caj_logger *log, void* data, int data_len);
unsigned char* vm_serialise_script(script_state *st, size_t *len);
void vm_free_script(script_state * st);

int vm_script_is_idle(script_state *st);
int vm_script_is_runnable(script_state *st);
int vm_script_has_failed(script_state *st);
char* vm_script_get_error(script_state *st);

void vm_prepare_script(script_state *st, void *priv, vm_world *w);
void vm_run_script(script_state *st, int num_steps);
int vm_event_has_handler(script_state *st, int event_id);
void vm_call_event(script_state *st, int event_id, ...);
int32_t vm_list_get_count(heap_header *list);
uint8_t vm_list_get_type(heap_header *list, int32_t pos);
char *vm_list_get_str(heap_header *list, int32_t pos);
int32_t vm_list_get_int(heap_header *list, int32_t pos);
float vm_list_get_float(heap_header *list, int32_t pos);
void vm_list_get_vector(heap_header *list, int32_t pos, caj_vector3* out);
void vm_func_get_args(script_state *st, int func_no, ...);
void vm_func_set_int_ret(script_state *st, int func_no, int32_t ret);
void vm_func_set_float_ret(script_state *st, int func_no, float ret);
void vm_func_set_str_ret(script_state *st, int func_no, const char* ret);
void vm_func_set_key_ret(script_state *st, int func_no, const uuid_t ret);
void vm_func_set_vect_ret(script_state *st, int func_no, const caj_vector3 *vect);
void vm_func_set_rot_ret(script_state *st, int func_no, const caj_quat *rot);
void vm_func_return(script_state *st, int func_no);

#endif
