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

struct script_func { // temporary placeholder
  uint32_t ip;
  int frame_sz;
};

struct script_state {
  uint32_t ip;
  uint16_t* bytecode;
  int32_t* stack_top;
  int32_t* frame;
  int32_t* globals;
  uint32_t* heap;
  script_func *funcs;
  int scram_flag;
};

#endif
