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

#ifndef CAJ_LSL_PARSE_H
#define CAJ_LSL_PARSE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

 typedef struct expr_node expr_node;

 typedef struct list_node {
   expr_node *expr;
   struct list_node* next;
 } list_node;

struct expr_node {
  int node_type; uint8_t vtype;
  union {
    expr_node* child[4];
    int i; float f; char* s; list_node *list; float v[4];
    struct { char* name; list_node* args; } call;
  } u;
};

#define STMT_EXPR 1
#define STMT_IF 2
#define STMT_RET 3
#define STMT_WHILE 4
#define STMT_DO 5
#define STMT_FOR 6
#define STMT_DECL 7 /* variable declaration */
#define STMT_BLOCK 8 /* { foo; bar; } block */
#define STMT_JUMP 9
#define STMT_LABEL 10

 typedef struct statement {
   int stype; /* STMT_* */
   expr_node *expr[3];
   struct statement *child[2];
   struct statement *next;
   char *s; /* only used for labels */
 } statement;

 typedef struct basic_block {
   statement *first;
   statement **add_here;
 } basic_block;

 typedef struct func_arg {
   uint8_t vtype;
   char *name;
   struct func_arg *next;
 } func_arg;

 typedef struct function {
   char *name;
   func_arg *args;
   basic_block *code;
   uint8_t ret_type;
   struct function *next;
 } function;

  typedef struct global {
    uint8_t vtype;
    char *name;
    expr_node *val;
    struct global *next;
  } global;

 typedef struct lsl_program {
   function *funcs;
   global *globals;
 } lsl_program;

 /* constants/variables */
#define NODE_CONST 1
#define NODE_IDENT 2
 /* binary operations */
#define NODE_ADD 3
#define NODE_SUB 4
#define NODE_MUL 5
#define NODE_DIV 6
#define NODE_MOD 7
#define NODE_ASSIGN 8
#define NODE_EQUAL 9
#define NODE_NEQUAL 10
#define NODE_LEQUAL 11
#define NODE_GEQUAL 12
#define NODE_LESS 13
#define NODE_GREATER 14
#define NODE_OR 15
#define NODE_AND 16
#define NODE_XOR 17
#define NODE_L_OR 18
#define NODE_L_AND 19
#define NODE_SHR 20
#define NODE_SHL 21
#define NODE_ASSIGNADD 22
#define NODE_ASSIGNSUB 23
#define NODE_ASSIGNMUL 24
#define NODE_ASSIGNDIV 25
#define NODE_ASSIGNMOD 26
 /* unary ops */
#define NODE_NEGATE 27
#define NODE_NOT 28 /* ~ - bitwise not */
#define NODE_L_NOT 29 /* ! - logical not */
#define NODE_PREINC 30 /* ++foo */
#define NODE_POSTINC 31 /* foo++ */
#define NODE_PREDEC 32 /* --foo */
#define NODE_POSTDEC 33 /* foo-- */

#define NODE_CALL 34 /* procedure call */
#define NODE_VECTOR 35 
#define NODE_ROTATION 36
#define NODE_LIST 37
#define NODE_CAST 38 /* special type of unary op */

  static const char* node_names[] = { "BOGUS", "const","ident","+","-","*","/","%","=",
				    "==","!=","<=",">=","<",">","|","&","^",
				    "||","&&","<<",">>","+=","-=","*=","/=",
				    "%=", "unary -", "~","!","preinc","postinc"
				    "predec","postdec","call","vec","rot",
				      "list","cast" };

 /* FIXME - types should be in a shared header */
#define VM_TYPE_NONE  0
#define VM_TYPE_INT   1
#define VM_TYPE_FLOAT 2
#define VM_TYPE_STR   3
#define VM_TYPE_KEY   4
#define VM_TYPE_VECT  5
#define VM_TYPE_ROT   6
#define VM_TYPE_LIST  7

static const char* type_names[] = {"void","int","float","str","key","vect","rot","list"};

  typedef struct lsl_const {
    const char* name;
    uint8_t vtype;
    union {
      int32_t i; float f; const char *s; float v[4];
    } u;
  } lsl_const;

  lsl_program *caj_parse_lsl(const char* fname);
  expr_node *enode_cast(expr_node *expr, uint8_t vtype);
  const lsl_const* find_lsl_const(const char* name);

#ifdef __cplusplus
}
#endif

#endif
