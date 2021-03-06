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

%{
#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include "caj_lsl_parse.h"
int yylex (void);
void yyerror (char const *);
int yydebug;

typedef struct lsl_location YYLTYPE;
# define YYLTYPE_IS_DECLARED 1


 typedef struct list_head {
   struct list_node* first;
   struct list_node** add_here;
 } list_head;


 static void enode_set_loc(struct expr_node *enode, YYLTYPE *loc) {
   enode->loc = *loc;
 }

 static expr_node * enode_make_int(char *s, YYLTYPE *loc) {
  struct expr_node *enode = malloc(sizeof(struct expr_node));
  enode->u.i = strtol(s, NULL, 0); // do we really want octal? Hmmm...
  enode->node_type = NODE_CONST; enode_set_loc(enode, loc);
  enode->vtype = VM_TYPE_INT;
  free(s);
  return enode;
}

static expr_node * enode_make_float(char *s, YYLTYPE *loc) {
  struct expr_node *enode = malloc(sizeof(struct expr_node));
  enode->u.f = strtof(s, NULL); enode_set_loc(enode, loc);
  enode->node_type = NODE_CONST;
  enode->vtype = VM_TYPE_FLOAT;
  free(s);
  return enode;
}

static expr_node * enode_make_str(char *s, YYLTYPE *loc) {
  struct expr_node *enode = malloc(sizeof(struct expr_node));
  enode->u.s = s; enode_set_loc(enode, loc);
  enode->node_type = NODE_CONST;
  enode->vtype = VM_TYPE_STR;
  return enode;
}

/* FIXME - propagate constants down to this node? */
 static expr_node * enode_make_vect(expr_node *x, expr_node *y, expr_node *z,
				    YYLTYPE *loc) {
  struct expr_node *enode = malloc(sizeof(struct expr_node));
  enode->node_type = NODE_VECTOR; enode_set_loc(enode, loc);
  enode->vtype = VM_TYPE_VECT;
  enode->u.child[0] = x; enode->u.child[1] = y; enode->u.child[2] = z;
  return enode;
}

 static expr_node * enode_make_rot(expr_node *x, expr_node *y, 
				   expr_node *z, expr_node *w,
				    YYLTYPE *loc) {
  struct expr_node *enode = malloc(sizeof(struct expr_node));
  enode->node_type = NODE_ROTATION; enode_set_loc(enode, loc);
  enode->vtype = VM_TYPE_ROT;
  enode->u.child[0] = x; enode->u.child[1] = y; 
  enode->u.child[2] = z; enode->u.child[3] = w;
  return enode;
}

 static expr_node * enode_make_list(list_node *list, YYLTYPE *loc) {
  struct expr_node *enode = malloc(sizeof(struct expr_node));
  enode->u.list = list; enode_set_loc(enode, loc);
  enode->node_type = NODE_LIST;
  enode->vtype = VM_TYPE_LIST;
  return enode;
}


 static expr_node * enode_make_id(char *s, char *item, YYLTYPE *loc) {
   struct expr_node *enode = malloc(sizeof(struct expr_node));
   const lsl_const *c = find_lsl_const(s);
   enode_set_loc(enode, loc);
   if(c == NULL) {
     enode->node_type = NODE_IDENT;
     enode->u.ident.name = s;
     enode->u.ident.item = item;
   } else { // FIXME - this could lead to assertion failures elsewhere!
     free(s);
     enode->node_type = NODE_CONST; enode->vtype = c->vtype;
     switch(c->vtype) {
     case VM_TYPE_INT:
       enode->u.i = c->u.i; break;
     case VM_TYPE_FLOAT:
       enode->u.f = c->u.f; break;     
     case VM_TYPE_STR:
       enode->u.s = strdup(c->u.s); break;     
     case VM_TYPE_ROT:
       enode->u.v[3] = c->u.v[3];
       // fall through
     case VM_TYPE_VECT:
       enode->u.v[0] = c->u.v[0]; enode->u.v[1] = c->u.v[1]; 
       enode->u.v[2] = c->u.v[2];
       break;
     default:
       yyerror("Unhandled type of named constant");
       enode->vtype = VM_TYPE_NONE; 
     }
   }
   return enode;
}

 static expr_node * enode_make_id_type(char *s, uint8_t vtype, YYLTYPE *loc) {
   struct expr_node *enode = malloc(sizeof(struct expr_node));
   enode->node_type = NODE_IDENT; enode->vtype = vtype;
   enode->u.ident.name = s;
   enode->u.ident.item = NULL; // FIXME
   enode_set_loc(enode, loc);
   return enode;
}

 static expr_node *enode_make_call(char* name, list_node* args, YYLTYPE *loc) {
   struct expr_node *enode = malloc(sizeof(struct expr_node));
   enode->node_type = NODE_CALL; enode->u.call.name = name;
   enode->u.call.args = args; enode_set_loc(enode, loc);
   return enode;
 }

 static  expr_node * enode_binop(expr_node *l, expr_node *r, int node_type, YYLTYPE *loc) {
    expr_node *enode = malloc(sizeof(expr_node));
    enode->node_type = node_type; enode_set_loc(enode, loc);
    enode->u.child[0] = l; enode->u.child[1] = r;
    return enode;
}

 static  expr_node * enode_unaryop(expr_node *expr, int node_type, YYLTYPE *loc) {
    expr_node *enode = malloc(sizeof(expr_node));
    enode->node_type = node_type; enode->vtype = expr->vtype;
    enode->u.child[0] = expr; enode_set_loc(enode, loc);
    return enode;  
}

 static expr_node *enode_clone_id(expr_node *expr) {
   struct expr_node *enode = malloc(sizeof(struct expr_node));
   assert(expr->node_type == NODE_IDENT);
   enode->node_type = NODE_IDENT; 
   enode->u.ident.name = strdup(expr->u.ident.name);
   enode->u.ident.item = (expr->u.ident.item == NULL ? NULL : 
			  strdup(expr->u.ident.item));
   enode->vtype = expr->vtype; enode->loc = expr->loc;
   return enode;
 }

 void enode_split_assign(expr_node *expr) {
   expr_node *id2, *binop;
   id2 = enode_clone_id(expr->u.child[0]);
   switch(expr->node_type) {
   case NODE_ASSIGNADD:
     binop = enode_binop(id2, expr->u.child[1], NODE_ADD, &expr->loc);
     break;
   case NODE_ASSIGNSUB:
     binop = enode_binop(id2, expr->u.child[1], NODE_SUB, &expr->loc);
     break;
   case NODE_ASSIGNMUL:
     binop = enode_binop(id2, expr->u.child[1], NODE_MUL, &expr->loc);
     break;
   case NODE_ASSIGNDIV:
     binop = enode_binop(id2, expr->u.child[1], NODE_DIV, &expr->loc);
     break;
   case NODE_ASSIGNMOD:
     binop = enode_binop(id2, expr->u.child[1], NODE_MOD, &expr->loc);
     break;
   default:
     assert(0);
   }
   expr->u.child[1] = binop; expr->node_type = NODE_ASSIGN;
 }

expr_node *enode_cast(expr_node *expr, uint8_t vtype) {
  expr_node *new_node;

  if(vtype == expr->vtype) return expr;
  if(expr->node_type == NODE_CONST) {
    switch(expr->vtype) {
    case VM_TYPE_INT:
      switch(vtype) {
      case VM_TYPE_FLOAT:
	expr->vtype = vtype; expr->u.f = expr->u.i;
	return expr;
      }
    }
  }
  new_node = enode_unaryop(expr, NODE_CAST, &expr->loc);
  new_node->vtype = vtype; return new_node;
}

static expr_node *enode_negate(expr_node *expr, YYLTYPE *loc) {
  if(expr->node_type == NODE_CONST) {
    switch(expr->vtype) {
    case VM_TYPE_INT:
      expr->u.i = -expr->u.i;
      return expr;
    case VM_TYPE_FLOAT:
      expr->u.f = -expr->u.f;
      return expr;
    default: break;
    }
  }
  return enode_unaryop(expr, NODE_NEGATE, loc);
}

 static statement* new_statement(int stype, YYLTYPE *loc) {
  statement *statem = malloc(sizeof(statement));
  statem->stype = stype; statem->loc = *loc;
  statem->next = NULL; 
  return statem;
}

 static list_node* make_list_entry(expr_node *expr) {
   list_node* lnode = malloc(sizeof(list_node));
   lnode->expr = expr; lnode->next = NULL;
   return lnode;
 }

 struct lsl_program global_prog; // FIXME - remove this

typedef struct func_args {
  func_arg *first; func_arg **add;
}  func_args;

 typedef struct lsl_globals {
   function *funcs;
   function **add_func;
   global *globals;
   global **add_global;
 } lsl_globals;

%}
%locations
 /* %debug */
%error-verbose
%union {
  struct expr_node *enode;
  struct statement *stat;
  struct basic_block *bblock;
  struct func_arg *arg;
  struct func_args *args;
  struct function *func;
  struct list_head *list;
  struct lsl_globals *prog;
  struct lsl_state *state;
  struct global *glob;
  char *str;
  uint8_t vtype;
}
%token IF ELSE WHILE FOR DO STATE DEFAULT RETURN JUMP
%token INCR DECR SHLEFT SHRIGHT /* ++ -- << >> */
%token L_AND L_OR EQUAL NEQUAL /* && || == != */
%token LEQUAL GEQUAL /* <= >= */
%token ASSIGNADD ASSIGNSUB ASSIGNMUL ASSIGNDIV ASSIGNMOD
%token <str> IDENTIFIER
%token <str> NUMBER REAL STR
%token <vtype> INTEGER FLOAT STRING KEY VECTOR ROTATION LIST /* LSL types */
%left '=' ASSIGNADD ASSIGNSUB ASSIGNMUL ASSIGNDIV ASSIGNMOD
%left L_OR
%left L_AND /* FIXME - the LSL wiki is contradictory as to the precidence of && and || */
%left '|'
%left '^'
%left '&'
%left EQUAL NEQUAL
%left '<' LEQUAL '>' GEQUAL
%left SHLEFT SHRIGHT
%left '-' '+'
%left '*' '/' '%'
%left NEG /* FIXME - where does this go? */
%left CAST /* FIXME - where does this go? */
%left '!' '~' INCR DECR
%type <str> state_id;
%type <enode> scexpr expr opt_expr variable call
%type <bblock> statements function_body
%type <stat> statement if_stmt while_stmt do_stmt for_stmt ret_stmt local
%type <stat> jump_stmt label_stmt block_stmt state_stmt
%type <func> function state_func
%type <state> state_funcs states
%type <prog> program functions
%type <glob> global
%type <arg> arguments argument
%type <args> arglist
%type <vtype> type 
%type <list> list

/* 1 shift-reduce conflict due to dangling else problem
 * 2 due to vector/rotation literals - FIXME figure out why exactly!
 */
%expect 3
%%
program : functions states { 
  $$ = NULL; global_prog.funcs = $1->funcs; global_prog.globals = $1->globals;
  global_prog.states = $2;
  free($1);
}; 
global : type IDENTIFIER ';'  {
  $$ = malloc(sizeof(global));
  $$->vtype = $1; $$->next = NULL;  $$->name = $2; $$->val = NULL;
 }
       | type IDENTIFIER '=' expr ';' { 
  $$ = malloc(sizeof(global));
  $$->vtype = $1; $$->next = NULL;  $$->name = $2; $$->val = $4;

 }  ; 
functions : /* nowt */ { 
  $$ = malloc(sizeof(lsl_globals)); $$->funcs = NULL; $$->add_func = &$$->funcs;
   $$->globals = NULL; $$->add_global = &$$->globals;
} 
| functions function { *($1->add_func) = $2; $1->add_func = &$2->next; $$ = $1; }
| functions global { *($1->add_global) = $2; $1->add_global = &$2->next; $$ = $1; };
function : type IDENTIFIER '(' arguments ')' function_body {
  $$ = malloc(sizeof(function));
  $$->ret_type = $1; $$->next = NULL;
  $$->name = $2; $$->args = $4; $$->code = $6;
}     | IDENTIFIER '(' arguments ')' function_body {
  /* ideally, we'd define "ret_type : | type" and avoid the code duplication,
     but this causes a fatal shift/reduce conflict with the def. of global */
  $$ = malloc(sizeof(function));
  $$->ret_type = VM_TYPE_NONE; $$->next = NULL;
  $$->name = $1; $$->args = $3; $$->code = $5;
  } ;
/* definition of states loses state ordering */
states : /* nothing */ { $$=NULL; } 
       | states state_id '{' state_funcs '}' { $4->next = $1; $4->name = $2; $$ = $4; };
state_id : DEFAULT { $$ = NULL; }
         | STATE IDENTIFIER { $$ = $2; } ;
state_funcs : /* nothing */ { 
  $$ = malloc(sizeof(lsl_state)); $$->funcs = NULL; $$->add_func = &$$->funcs; 
}
            | state_funcs state_func { *$1->add_func = $2; $1->add_func = &$2->next; $$ = $1 } ;
state_func : IDENTIFIER '(' arguments ')' function_body{
  /* FIXME - code duplication! */
  $$ = malloc(sizeof(function));
  $$->ret_type = VM_TYPE_NONE; $$->next = NULL;
  $$->name = $1; $$->args = $3; $$->code = $5;
  } ;
arguments : /* nothing */ { $$ = NULL; }
          | arglist { $$ = $1->first; free($1); } ;
arglist : argument { $$ = malloc(sizeof(func_args)); $$->first = $1; $$->add = &$1->next; }
     | arglist ',' argument { *($1->add) = $3; $1->add = &$3->next; $$ = $1; } ;
argument: type IDENTIFIER { 
  $$ = malloc(sizeof(func_arg)); $$->vtype = $1; 
  $$->name = $2; $$->next = NULL;
}
function_body : '{' statements '}' { $$ = $2 };
statements : /* nothing */ { $$ = malloc(sizeof(basic_block)); $$->first = NULL;
                             $$->add_here = &($$->first) }
| statements statement { 
  if($2 != NULL) { *($1->add_here) = $2; $1->add_here = &$2->next; }
  $$ = $1;
 } ;
statement : ';' { $$ = NULL } |  expr ';' { 
  $$ = new_statement(STMT_EXPR, &@1);
  $$->expr[0] = $1; 
 } 
            | if_stmt
            | do_stmt 
	    | while_stmt
	    | for_stmt
	    | ret_stmt ';'
            | local ';'
            | jump_stmt
            | label_stmt
	    | block_stmt 
	    | state_stmt
	    ;
block_stmt : '{' statements '}' { 
  $$ = new_statement(STMT_BLOCK, &@1);
  $$->child[0] = $2->first; free($2);
 } ;
local : type IDENTIFIER {
  $$ = new_statement(STMT_DECL, &@1);
  $$->expr[0] = enode_make_id_type($2,$1, &@1); $$->expr[1] = NULL;
 }
    | type IDENTIFIER '=' expr {
  $$ = new_statement(STMT_DECL, &@1);
  $$->expr[0] = enode_make_id_type($2,$1, &@1); $$->expr[1] = $4;
 }; 
if_stmt : IF '(' expr ')' statement {
  $$ = new_statement(STMT_IF, &@1); $$->expr[0] = $3;
  $$->child[0] = $5; $$->child[1] = NULL;
 }
        | IF '(' expr ')' statement ELSE statement {
  $$ = new_statement(STMT_IF, &@1); $$->expr[0] = $3;
  $$->child[0] = $5; $$->child[1] = $7; 
 }   ;
while_stmt : WHILE '(' expr ')' statement {
  $$ = new_statement(STMT_WHILE, &@1);
  $$->expr[0] = $3; $$->child[0] = $5;
 };
do_stmt : DO statement WHILE '(' expr ')' {
  $$ = new_statement(STMT_DO, &@1);
  $$->child[0] = $2; $$->expr[0] = $5;
 };
for_stmt : FOR '(' opt_expr ';' opt_expr ';' opt_expr ')'  statement {
  $$ = new_statement(STMT_FOR, &@1);
  $$->expr[0] = $3; $$->expr[1] = $5; $$->expr[2] = $7; 
  $$->child[0] = $9; 
 } ;
jump_stmt : JUMP IDENTIFIER ';' {
  $$ = new_statement(STMT_JUMP, &@1); $$->s = $2;
 } ;
label_stmt : '@' IDENTIFIER ';'{
  $$ = new_statement(STMT_LABEL, &@1); $$->s = $2;
 } ; 
state_stmt : STATE IDENTIFIER ';'{
  $$ = new_statement(STMT_STATE, &@1);  $$->s = $2;
 } 
    | STATE DEFAULT {
  $$ = new_statement(STMT_STATE, &@1); $$->s = strdup("default");
 }
 ; 
opt_expr : /* nothing */ { $$ = NULL } | expr ;
ret_stmt : RETURN { $$ = new_statement(STMT_RET, &@1); $$->expr[0] = NULL; }
         | RETURN expr { $$ = new_statement(STMT_RET, &@1); $$->expr[0] = $2; }
         ;
variable: IDENTIFIER { $$ = enode_make_id($1, NULL, &@1); }
| IDENTIFIER '.' IDENTIFIER { $$ = enode_make_id($1, $3, &@1); }
   ; 
call : IDENTIFIER '(' list ')' { $$ = enode_make_call($1,$3->first, &@1); free($3); }
list : { $$ = malloc(sizeof(list_head)); $$->first = NULL; $$->add_here = &$$->first; }
       | expr { 
	 $$ = malloc(sizeof(list_head)); $$->first = make_list_entry($1);
	 $$->add_here = &($$->first->next);
	 }
       | list ',' expr { 
	 list_node *lnode = make_list_entry($3);
	 *($1->add_here) = lnode; $1->add_here = &lnode->next; $$ = $1;
      } ;
/* Dirty hack to avoid nasty parser issues. If we have:
      <1.5, 2.5, 3.5> - <4.5, 5.5, 6.5>)
   this gets parsed as:
      <1.5, 2.5, ( 3.5 > -<4.5, 5.5, 6.5> )
   which is wrong. This hack fixes the issue by forbidding < and > directly 
   within a list constant, which the language technically should allow.
   If you really need to do this, use braces. 
 */
expr : scexpr 
       | expr '<' expr { $$ = enode_binop($1,$3,NODE_LESS, &@2); }
       | expr '>' expr { $$ = enode_binop($1,$3,NODE_GREATER, &@2); }
;
scexpr : NUMBER { $$ = enode_make_int($1, &@1); }
       | REAL { $$ = enode_make_float($1, &@1); }
       | STR { $$ = enode_make_str($1, &@1); }
       | call { $$ = $1; } 
       | variable { $$ = $1; }
       | variable '=' expr { $$ = enode_binop($1,$3,NODE_ASSIGN, &@2); }
       | variable ASSIGNADD expr { $$ = enode_binop($1,$3,NODE_ASSIGNADD, &@2); }
       | variable ASSIGNSUB expr { $$ = enode_binop($1,$3,NODE_ASSIGNSUB, &@2); }
       | variable ASSIGNMUL expr { $$ = enode_binop($1,$3,NODE_ASSIGNMUL, &@2); }
       | variable ASSIGNDIV expr { $$ = enode_binop($1,$3,NODE_ASSIGNDIV, &@2); }
       | variable ASSIGNMOD expr { $$ = enode_binop($1,$3,NODE_ASSIGNMOD, &@2); }
       | expr '+' expr { $$ = enode_binop($1,$3,NODE_ADD, &@2); }
       | expr '-' expr { $$ = enode_binop($1,$3,NODE_SUB, &@2); }
       | expr '*' expr { $$ = enode_binop($1,$3,NODE_MUL, &@2); }
       | expr '/' expr { $$ = enode_binop($1,$3,NODE_DIV, &@2); }
       | expr '%' expr { $$ = enode_binop($1,$3,NODE_MOD, &@2); }
       | expr EQUAL expr { $$ = enode_binop($1,$3,NODE_EQUAL, &@2); }
       | expr NEQUAL expr { $$ = enode_binop($1,$3,NODE_NEQUAL, &@2); }
       | expr LEQUAL expr { $$ = enode_binop($1,$3,NODE_LEQUAL, &@2); }
       | expr GEQUAL expr { $$ = enode_binop($1,$3,NODE_GEQUAL, &@2); }
       | expr '|' expr { $$ = enode_binop($1,$3,NODE_OR, &@2); }
       | expr '&' expr { $$ = enode_binop($1,$3,NODE_AND, &@2); }
       | expr '^' expr { $$ = enode_binop($1,$3,NODE_XOR, &@2); }
       | expr L_OR expr { $$ = enode_binop($1,$3,NODE_L_OR, &@2); }
       | expr L_AND expr { $$ = enode_binop($1,$3,NODE_L_AND, &@2); }
       | expr SHLEFT expr { $$ = enode_binop($1,$3,NODE_SHL, &@2); }
       | expr SHRIGHT expr { $$ = enode_binop($1,$3,NODE_SHR, &@2); }
       | '(' expr ')' { $$ = $2; }
| '<' scexpr ',' scexpr  ',' scexpr ',' scexpr '>'{ $$ = enode_make_rot($2,$4,$6,$8, &@1); } 
| '<' scexpr ',' scexpr  ',' scexpr '>' { $$ = enode_make_vect($2,$4,$6, &@1); } 
| '[' list ']' { $$ = enode_make_list($2->first, &@1); free($2); }
| variable INCR { $$ = enode_unaryop($1, NODE_POSTINC, &@1); } 
| variable DECR { $$ = enode_unaryop($1, NODE_POSTDEC, &@1); } 
| INCR variable { $$ = enode_unaryop($2, NODE_PREINC, &@1); }
| DECR variable { $$ = enode_unaryop($2, NODE_PREDEC, &@1); } 
| '!' expr { $$ = enode_unaryop($2, NODE_L_NOT, &@1); } 
| '~' expr { $$ = enode_unaryop($2, NODE_NOT, &@1); }
| '-' expr { $$ = enode_negate($2, &@1); } 
| '(' type ')' expr %prec CAST { $$ = enode_cast($4,$2); } 
       
   ;
type : INTEGER { $$ = VM_TYPE_INT; } 
     | FLOAT { $$ = VM_TYPE_FLOAT; } 
     | STRING { $$ = VM_TYPE_STR; } 
     | KEY { $$ = VM_TYPE_KEY; } 
     | VECTOR { $$ = VM_TYPE_VECT; } 
     | ROTATION { $$ = VM_TYPE_ROT; } 
     | LIST { $$ = VM_TYPE_LIST; } 
  ;
%%
#include <stdio.h>

#if 0 /* debugging code */
static void print_expr(expr_node *enode) {
  list_node *lnode; int i;
  if(enode == NULL) { printf("<NULL enode> "); return; }
  switch(enode->node_type) {
  case NODE_CONST:
    switch(enode->vtype) {
    case VM_TYPE_INT:
      printf("%i ", enode->u.i); break;
    case VM_TYPE_FLOAT:
      printf("%f ", (double)enode->u.f); break;
    case VM_TYPE_STR:
      printf("\"%s\" ", enode->u.s); break;
    default:
      printf("<unknown const %i> ", (int)enode->vtype); break;
    }
    break;
  case NODE_IDENT:
    printf("%s ", enode->u.ident.name); break;
  case NODE_ADD:
    printf("(add "); print_expr(enode->u.child[0]); 
    print_expr(enode->u.child[1]); printf(") ");
    break;
  case NODE_SUB:
    printf("(sub "); print_expr(enode->u.child[0]); 
    print_expr(enode->u.child[1]); printf(") ");
    break;
  case NODE_MUL:
    printf("(mul "); print_expr(enode->u.child[0]); 
    print_expr(enode->u.child[1]); printf(") ");
    break;
  case NODE_DIV:
    printf("(div "); print_expr(enode->u.child[0]); 
    print_expr(enode->u.child[1]); printf(") ");
    break;
  case NODE_MOD:
    printf("(mod "); print_expr(enode->u.child[0]); 
    print_expr(enode->u.child[1]); printf(") ");
    break;
  case NODE_ASSIGN:
    printf("(assign "); print_expr(enode->u.child[0]); 
    print_expr(enode->u.child[1]); printf(") ");
    break;
  case NODE_EQUAL:
    printf("(== "); print_expr(enode->u.child[0]); 
    print_expr(enode->u.child[1]); printf(") ");
    break;
  case NODE_NEQUAL:
    printf("(!= "); print_expr(enode->u.child[0]); 
    print_expr(enode->u.child[1]); printf(") ");
    break;
  case NODE_LEQUAL:
    printf("(<= "); print_expr(enode->u.child[0]); 
    print_expr(enode->u.child[1]); printf(") ");
    break;
  case NODE_GEQUAL:
    printf("(>= "); print_expr(enode->u.child[0]); 
    print_expr(enode->u.child[1]); printf(") ");
    break;
  case NODE_LESS:
    printf("(< "); print_expr(enode->u.child[0]); 
    print_expr(enode->u.child[1]); printf(") ");
    break;
  case NODE_GREATER:
    printf("(> "); print_expr(enode->u.child[0]); 
    print_expr(enode->u.child[1]); printf(") ");
    break;
  case NODE_OR:
    printf("(bit_or "); print_expr(enode->u.child[0]); 
    print_expr(enode->u.child[1]); printf(") ");
    break;
  case NODE_AND:
    printf("(bit_and "); print_expr(enode->u.child[0]); 
    print_expr(enode->u.child[1]); printf(") ");
    break;
  case NODE_XOR:
    printf("(bit_xor "); print_expr(enode->u.child[0]); 
    print_expr(enode->u.child[1]); printf(") ");
    break;
  case NODE_NOT:
    printf("(bit_not "); print_expr(enode->u.child[0]); 
    printf(") ");
    break;
  case NODE_L_OR:
    printf("(log_or "); print_expr(enode->u.child[0]); 
    print_expr(enode->u.child[1]); printf(") ");
    break;
  case NODE_L_AND:
    printf("(log_and "); print_expr(enode->u.child[0]); 
    print_expr(enode->u.child[1]); printf(") ");
    break;
  case NODE_L_NOT:
    printf("(log_not "); print_expr(enode->u.child[0]); 
    printf(") ");
    break;
  case NODE_CALL:
    printf("(call %s ", enode->u.call.name);
    for(lnode = enode->u.call.args; lnode != NULL; lnode = lnode->next)
      print_expr(lnode->expr);
    printf(") ");
    break;
  case NODE_ASSIGNADD:
    printf("(assignadd "); print_expr(enode->u.child[0]); 
    print_expr(enode->u.child[1]); printf(") ");
    break;
  case NODE_ASSIGNSUB:
    printf("(assignsub "); print_expr(enode->u.child[0]); 
    print_expr(enode->u.child[1]); printf(") ");
    break;
  case NODE_ASSIGNMUL:
    printf("(assignmul "); print_expr(enode->u.child[0]); 
    print_expr(enode->u.child[1]); printf(") ");
    break;
  case NODE_ASSIGNDIV:
    printf("(assigndiv "); print_expr(enode->u.child[0]); 
    print_expr(enode->u.child[1]); printf(") ");
    break;
  case NODE_ASSIGNMOD:
    printf("(assignmod "); print_expr(enode->u.child[0]); 
    print_expr(enode->u.child[1]); printf(") ");
    break;
  case NODE_PREINC:
    printf("(preinc "); print_expr(enode->u.child[0]); printf(") ");
    break;
  case NODE_POSTINC:
    printf("(postinc "); print_expr(enode->u.child[0]); printf(") ");
    break;
  case NODE_PREDEC:
    printf("(predec "); print_expr(enode->u.child[0]); printf(") ");
    break;
  case NODE_POSTDEC:
    printf("(postdec "); print_expr(enode->u.child[0]); printf(") ");
    break;
  case NODE_VECTOR:
    printf("(vector ");
    for(i = 0; i < 3; i++) print_expr(enode->u.child[i]); 
    printf(") ");
    break;
  case NODE_ROTATION:
    printf("(rotation ");
    for(i = 0; i < 4; i++) print_expr(enode->u.child[i]); 
    printf(") ");
    break;
  case NODE_LIST:
    printf("(list ");
    for(lnode = enode->u.list; lnode != NULL; lnode = lnode->next)
      print_expr(lnode->expr);
    printf(") ");
    break;
  case NODE_CAST:
    printf("(cast ??? "); print_expr(enode->u.child[0]); printf(") ");
    break;
  default:
    printf("<unknown expr %i> ",enode->node_type);
    break;
  }
}

static void print_stmts(statement *statem, int indent) {
  int i;
  for( ; statem != NULL; statem = statem->next) {
    for(i = 0; i < indent; i++) printf("  ");
    switch(statem->stype) {
    case STMT_EXPR:
      printf("expression ");
      print_expr(statem->expr[0]);
      break;
    case STMT_IF:
      printf("if "); print_expr(statem->expr[0]); printf("\n");
      print_stmts(statem->child[0], indent+1);
      if(statem->child[1] != NULL) {
	for(i = 0; i < indent; i++) printf("  ");
	printf("else\n");
	print_stmts(statem->child[1], indent+1);
      }
      for(i = 0; i < indent; i++) printf("  ");
      printf("end if");
      break;
    case STMT_RET:
      printf("return "); print_expr(statem->expr[0]);
      break;
    case STMT_WHILE:
      printf("while "); print_expr(statem->expr[0]); printf("\n");
      print_stmts(statem->child[0], indent+1);
      for(i = 0; i < indent; i++) printf("  ");
      printf("loop");
      break;
    case STMT_DO:
      printf("do\n");
      print_stmts(statem->child[0], indent+1);
      for(i = 0; i < indent; i++) printf("  ");
      printf("while "); print_expr(statem->expr[0]); 
      break;
    case STMT_FOR:
      printf("for "); print_expr(statem->expr[0]); printf("; ");
      print_expr(statem->expr[1]); printf("; ");
      print_expr(statem->expr[2]); printf("\n");
      for(i = 0; i < indent; i++) printf("  ");
      printf("end for");
      break;
      /* TODO */
    case STMT_DECL:
      assert(statem->expr[0]->node_type == NODE_IDENT);
      printf("decl %s %s", type_names[statem->expr[0]->vtype], 
	     statem->expr[0]->u.s);
      if(statem->expr[1] != NULL) {
	printf(" = "); print_expr(statem->expr[1]); 
      }
      break;
    case STMT_BLOCK:
      printf("{\n");
      print_stmts(statem->child[0], indent+1);
      for(i = 0; i < indent; i++) printf("  ");
      printf("}");
      break;
    default:
      printf("<unknown statement type %i>", statem->stype);
      break;
    }
    printf("\n");
  }
}
#endif

lsl_program *caj_parse_lsl(const char* fname) {
  extern FILE *yyin;
#if 0 // debugging code
  function *func;
#endif
  yylloc.first_line = yylloc.last_line = 0;
  yylloc.first_column = yylloc.last_column = 0;
  yydebug = 1;
  yyin = fopen(fname,"r");
  if(yyin == NULL) {
    printf("ERROR: file not found\n");
    return NULL;
  }
  global_prog.funcs = NULL;
  if(yyparse()) return NULL;

#if 0 // debugging code
  
   for(func = global_prog.funcs; func != NULL; func = func->next) {
     statement* statem; func_arg *arg;
     printf("%s %s(", type_names[func->ret_type],
           func->name);
     for(arg = func->args; arg != NULL; arg = arg->next) {
       printf("%s %s, ", type_names[arg->vtype], arg->name);
     }
     printf(") {\n");
     statem = func->code->first;
     print_stmts(statem, 1);
     printf("}\n");
   }
#endif

  return &global_prog;
}


void yyerror(char const *error) { 
  // FIXME - add column number
  printf("(%i, 0): %s\n", yylloc.first_line, error);
}

