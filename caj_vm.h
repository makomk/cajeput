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
// #define ICLASS_WRG_P  5
// #define ICLASS_RDG_V  6
// #define ICLASS_WRG_V  7
#define ICLASS_RDL_I 8
#define ICLASS_WRL_I 9

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

// todo - unitary negation!
#define INSN_NOOP   0
#define INSN_ABORT  1 // should never appear in actual bytecode!
#define INSN_ADD_II 2
#define INSN_SUB_II 3
#define INSN_MUL_II 4
#define INSN_DIV_II 5
#define INSN_ADD_FF 6
#define INSN_SUB_FF 7
#define INSN_MUL_FF 8
#define INSN_DIV_FF 9
#define INSN_RET    10
#define INSN_MOD_II 11
#define INSN_AND_II 12 // & - todo
#define INSN_OR_II  13 // | - todo
#define INSN_XOR_II 14 // ^ - todo
#define INSN_NOT_II 15 // ~ - todo
#define INSN_SHL    16 // << - todo
#define INSN_SHR    17 // >> - todo
#define INSN_AND_L  18 // && - non-short circuiting, of course
#define INSN_OR_L   19 // ||
#define INSN_NOT_L  20 // !
#define INSN_COND   21 // pops value from stack, skips next insn if it's 0
#define INSN_NCOND  22 // pops value from stack, skips next insn unless it's 0
#define INSN_EQ_II  23 // ==
#define INSN_NEQ_II 24 // !=
#define INSN_GR_II  25 // >
#define INSN_LES_II 26 // <
#define INSN_GEQ_II 27 // >=
#define INSN_LEQ_II 28 // <=
#define INSN_POP_I 29 
// #define INSN_POP_P 30
// #define INSN_POP_I3 31 // POP_I*3
// #define INSN_POP_I4 32 // POP_I*4
#define INSN_PRINT_I 33
#define INSN_PRINT_F 34
#define INSN_PRINT_STR 35

#define NUM_INSNS 36

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
#define VM_TYPE_RET_ADDR 100

struct insn_info {
  uint8_t special, arg1, arg2, ret;
};

#define IVERIFY_INVALID 0
#define IVERIFY_NORMAL 1
#define IVERIFY_COND 2
#define IVERIFY_RET 3

static const insn_info vm_insns[NUM_INSNS] = {
  { IVERIFY_NORMAL, VM_TYPE_NONE, VM_TYPE_NONE, VM_TYPE_NONE }, // NOOP
  { IVERIFY_INVALID, VM_TYPE_NONE, VM_TYPE_NONE, VM_TYPE_NONE }, // ABORT
  { IVERIFY_NORMAL, VM_TYPE_INT, VM_TYPE_INT, VM_TYPE_INT }, // ADD_II
  { IVERIFY_NORMAL, VM_TYPE_INT, VM_TYPE_INT, VM_TYPE_INT }, // SUB_II
  { IVERIFY_NORMAL, VM_TYPE_INT, VM_TYPE_INT, VM_TYPE_INT }, // MUL_II
  { IVERIFY_NORMAL, VM_TYPE_INT, VM_TYPE_INT, VM_TYPE_INT }, // DIV_II
  { IVERIFY_NORMAL, VM_TYPE_FLOAT, VM_TYPE_FLOAT, VM_TYPE_FLOAT }, // ADD_FF
  { IVERIFY_NORMAL, VM_TYPE_FLOAT, VM_TYPE_FLOAT, VM_TYPE_FLOAT }, // SUB_FF
  { IVERIFY_NORMAL, VM_TYPE_FLOAT, VM_TYPE_FLOAT, VM_TYPE_FLOAT }, // MUL_FF
  { IVERIFY_NORMAL, VM_TYPE_FLOAT, VM_TYPE_FLOAT, VM_TYPE_FLOAT }, // DIV_FF
  { IVERIFY_RET, VM_TYPE_NONE, VM_TYPE_NONE, VM_TYPE_NONE }, // RET
  { IVERIFY_NORMAL, VM_TYPE_INT, VM_TYPE_INT, VM_TYPE_INT }, // MOD_II
  { IVERIFY_NORMAL, VM_TYPE_INT, VM_TYPE_INT, VM_TYPE_INT }, // AND_II
  { IVERIFY_NORMAL, VM_TYPE_INT, VM_TYPE_INT, VM_TYPE_INT }, // OR_II
  { IVERIFY_NORMAL, VM_TYPE_INT, VM_TYPE_INT, VM_TYPE_INT }, // XOR_II
  { IVERIFY_NORMAL, VM_TYPE_INT, VM_TYPE_NONE, VM_TYPE_INT }, // NOT_I
  { IVERIFY_NORMAL, VM_TYPE_INT, VM_TYPE_INT, VM_TYPE_INT }, // SHR
  { IVERIFY_NORMAL, VM_TYPE_INT, VM_TYPE_INT, VM_TYPE_INT }, // SHL  
  { IVERIFY_NORMAL, VM_TYPE_INT, VM_TYPE_INT, VM_TYPE_INT }, // AND_L
  { IVERIFY_NORMAL, VM_TYPE_INT, VM_TYPE_INT, VM_TYPE_INT }, // OR_L
  { IVERIFY_NORMAL, VM_TYPE_INT, VM_TYPE_NONE, VM_TYPE_INT }, // NOT_L
  { IVERIFY_COND, VM_TYPE_INT, VM_TYPE_NONE, VM_TYPE_NONE }, // COND
  { IVERIFY_COND, VM_TYPE_INT, VM_TYPE_NONE, VM_TYPE_NONE }, // NCOND
  { IVERIFY_NORMAL, VM_TYPE_INT, VM_TYPE_INT, VM_TYPE_INT }, // EQ_II
  { IVERIFY_NORMAL, VM_TYPE_INT, VM_TYPE_INT, VM_TYPE_INT }, // NEQ_II
  { IVERIFY_NORMAL, VM_TYPE_INT, VM_TYPE_INT, VM_TYPE_INT }, // GR_II
  { IVERIFY_NORMAL, VM_TYPE_INT, VM_TYPE_INT, VM_TYPE_INT }, // LES_II
  { IVERIFY_NORMAL, VM_TYPE_INT, VM_TYPE_INT, VM_TYPE_INT }, // GEQ_II
  { IVERIFY_NORMAL, VM_TYPE_INT, VM_TYPE_INT, VM_TYPE_INT }, // LEQ_II
  { IVERIFY_NORMAL, VM_TYPE_INT, VM_TYPE_NONE, VM_TYPE_NONE }, // POP_I
  { IVERIFY_INVALID, VM_TYPE_NONE, VM_TYPE_NONE, VM_TYPE_NONE }, // POP_P - FIXME!
  { IVERIFY_INVALID, VM_TYPE_NONE, VM_TYPE_NONE, VM_TYPE_NONE }, // POP_I3 - FIXME!
  { IVERIFY_INVALID, VM_TYPE_NONE, VM_TYPE_NONE, VM_TYPE_NONE }, // POP_I4 - FIXME!  
  { IVERIFY_NORMAL, VM_TYPE_INT, VM_TYPE_NONE, VM_TYPE_NONE }, // PRINT_I
  { IVERIFY_NORMAL, VM_TYPE_FLOAT, VM_TYPE_NONE, VM_TYPE_NONE }, // PRINT_F
  { IVERIFY_NORMAL, VM_TYPE_STR, VM_TYPE_NONE, VM_TYPE_NONE }, // PRINT_STR
};

struct script_state {
  uint32_t ip;
  uint16_t* bytecode;
  int32_t* stack_top;
  int32_t* frame;
  int32_t* globals;
  uint32_t* heap;
  int scram_flag;
};

#endif
