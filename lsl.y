%{
#include <math.h>
#include <stdlib.h>
#include <stdint.h>
int yylex (void);
void yyerror (char const *);
int yydebug;

typedef struct expr_node {
  int node_type; uint8_t vtype;
  union {
    struct expr_node* child[2];
    int i; float f; char* s;
  } u;
} expr_node;

#define STMT_EXPR 1
#define STMT_IF 2
#define STMT_RET 3

 typedef struct statement {
   int stype; /* STMT_* */
   expr_node *expr;
   struct statement *next;
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
 } function;

 typedef struct lsl_program {
   function *func; // FIXME
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
 /* mono ops */
#define NODE_NEGATE 26
#define NODE_NOT 27 /* ~ - bitwise not */
#define NODE_L_NOT 28 /* ! - logical not */

 /* FIXME - types should be in a shared header */
#define VM_TYPE_NONE  0
#define VM_TYPE_INT   1
#define VM_TYPE_FLOAT 2
#define VM_TYPE_STR   3
#define VM_TYPE_KEY   4
#define VM_TYPE_VECT  5
#define VM_TYPE_ROT   6

 const char* type_names[] = {"void","int","float","str","key","vect","rot"};

static expr_node * enode_make_int(char *s) {
  struct expr_node *enode = malloc(sizeof(struct expr_node));
  enode->u.i = strtol(s, NULL, 0); // do we really want octal? Hmmm...
  enode->node_type = NODE_CONST;
  enode->vtype = VM_TYPE_INT;
  free(s);
  return enode;
}

static expr_node * enode_make_float(char *s) {
  struct expr_node *enode = malloc(sizeof(struct expr_node));
  enode->u.f = strtof(s, NULL);
  enode->node_type = NODE_CONST;
  enode->vtype = VM_TYPE_FLOAT;
  free(s);
  return enode;
}

static expr_node * enode_make_id(char *s) {
   struct expr_node *enode = malloc(sizeof(struct expr_node));
   enode->node_type = NODE_IDENT;
   enode->u.s = s;
    return enode;
}

static  expr_node * enode_binop(expr_node *l, expr_node *r, int node_type) {
    expr_node *enode = malloc(sizeof(expr_node));
    enode->node_type = node_type;
    enode->u.child[0] = l; enode->u.child[1] = r; 
    return enode;
}

static  expr_node * enode_monop(expr_node *expr, int node_type) {
    expr_node *enode = malloc(sizeof(expr_node));
    enode->node_type = node_type; enode->vtype = expr->vtype;
    enode->u.child[0] = expr;
    return enode;
}

static expr_node *enode_negate(expr_node *expr) {
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
  return enode_monop(expr, NODE_NEGATE);
}


 struct lsl_program global_prog; // FIXME - remove this

%}
%locations
%debug
%error-verbose
%union {
  struct expr_node *enode;
  struct statement *stat;
  struct basic_block *bblock;
  struct func_arg *arg;
  struct function *func;
  char *str;
  uint8_t vtype;
}
%token IF ELSE WHILE FOR STATE DEFAULT RETURN
%token INCR DECR SHLEFT SHRIGHT /* ++ -- << >> */
%token L_AND L_OR EQUAL NEQUAL /* && || == != */
%token LEQUAL GEQUAL /* <= >= */
%token ASSIGNADD ASSIGNSUB ASSIGNMUL ASSIGNDIV ASSIGNMOD
%token <str> IDENTIFIER
%token <str> NUMBER REAL STR
%token <vtype> INTEGER FLOAT STRING KEY VECTOR ROTATION LIST /* LSL types */
%left '=' ASSIGNADD ASSIGNSUB ASSIGNMUL ASSIGNDIV ASSIGNMOD // FIXME - add other assignment ops
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
%type <enode> expr
%type <bblock> statements function_body
%type <func> function program functions
%type <arg> arguments arglist argument
%type <vtype> type 
%type <str> variable /* FIXME - will have to change this! */
%%
program : functions states { $$ = $1; global_prog.func = $1; }; 
global : type IDENTIFIER ';' | type IDENTIFIER '=' expr ';' ; 
functions : /* nowt */ { $$ = NULL } | functions function { $$=$2; } | functions global ;
function : type IDENTIFIER '(' arguments ')' function_body {
  $$ = malloc(sizeof(function));
  $$->ret_type = $1;
  $$->name = $2; $$->args = $4; $$->code = $6;
}     | IDENTIFIER '(' arguments ')' function_body {
  /* ideally, we'd define "ret_type : | type" and avoid the code duplication,
     but this causes a fatal shift/reduce conflict with the def. of global */
  $$ = malloc(sizeof(function));
  $$->ret_type = VM_TYPE_NONE;
  $$->name = $1; $$->args = $3; $$->code = $5;
  } ;
states : /* nothing */ | states state_id '{' state_funcs '}';
state_id : DEFAULT { $$ = NULL; }
         | STATE IDENTIFIER { $$ = $2; } ;
state_funcs : /* nothing */ | state_funcs state_func ;
state_func : IDENTIFIER '(' arguments ')' function_body;
arguments : /* nothing */ { $$ = NULL; }
          | arglist ;
arglist : argument /* FIXME - this production feels inefficient? */
        | arglist ',' argument { $1->next = $3; $$ = $1 } ;
argument: type IDENTIFIER { 
  $$ = malloc(sizeof(func_arg)); $$->vtype = $1; 
  $$->name = $2; $$->next = NULL;
}
function_body : '{' statements '}' { $$ = $2 };
statements : /* nothing */ { $$ = malloc(sizeof(basic_block)); $$->first = NULL;
                             $$->add_here = &($$->first) }
| statements ';' { $$ = $1 }
| statements expr ';' { 
  statement *statem = malloc(sizeof(statement));
  statem->expr = $2; statem->next = NULL; statem->stype = STMT_EXPR;
  *($1->add_here) = statem; $1->add_here = &statem->next;
  $$ = $1;
} 
| statements if_stmt ; // FIXME 
| statements ret_stmt ';' ; // FIXME
| statements local ';' ;
local : type IDENTIFIER | type IDENTIFIER '=' expr ; 
if_stmt : IF '(' expr ')' '{' statements '}'
        | IF '(' expr ')' '{' statements '}' ELSE '{' statements '}';
ret_stmt : RETURN | RETURN expr ;
variable: IDENTIFIER | IDENTIFIER '.' IDENTIFIER; /* FIXME - handle .x right */
call_args : /* nowt */ | expr | call_args ',' expr  ;
call : IDENTIFIER '(' call_args ')'
list : | expr | list ',' expr ; // FIXME. Also, merge with argument list?
expr : NUMBER { $$ = enode_make_int($1); }
       | REAL { $$ = enode_make_float($1); }
       | STR { $$ = NULL; /* FIXME */ }
       | call { $$ = NULL; } // FIXME
       | variable { $$ = enode_make_id($1); }
       | variable '=' expr { $$ = enode_binop(enode_make_id($1),$3,NODE_ASSIGN); }
       | variable ASSIGNADD expr { $$ = enode_binop(enode_make_id($1),$3,NODE_ASSIGNADD); }
       | variable ASSIGNSUB expr { $$ = enode_binop(enode_make_id($1),$3,NODE_ASSIGNSUB); }
       | variable ASSIGNMUL expr { $$ = enode_binop(enode_make_id($1),$3,NODE_ASSIGNMUL); }
       | variable ASSIGNDIV expr { $$ = enode_binop(enode_make_id($1),$3,NODE_ASSIGNDIV); }
       | expr '+' expr { $$ = enode_binop($1,$3,NODE_ADD); }
       | expr '-' expr { $$ = enode_binop($1,$3,NODE_SUB); }
       | expr '*' expr { $$ = enode_binop($1,$3,NODE_MUL); }
       | expr '/' expr { $$ = enode_binop($1,$3,NODE_DIV); }
       | expr EQUAL expr { $$ = enode_binop($1,$3,NODE_EQUAL); }
       | expr NEQUAL expr { $$ = enode_binop($1,$3,NODE_NEQUAL); }
       | expr LEQUAL expr { $$ = enode_binop($1,$3,NODE_LEQUAL); }
       | expr GEQUAL expr { $$ = enode_binop($1,$3,NODE_GEQUAL); }
       | expr '<' expr { $$ = enode_binop($1,$3,NODE_LESS); }
       | expr '>' expr { $$ = enode_binop($1,$3,NODE_GREATER); }
       | expr '|' expr { $$ = enode_binop($1,$3,NODE_OR); }
       | expr '&' expr { $$ = enode_binop($1,$3,NODE_AND); }
       | expr '^' expr { $$ = enode_binop($1,$3,NODE_XOR); }
       | expr L_OR expr { $$ = enode_binop($1,$3,NODE_L_OR); }
       | expr L_AND expr { $$ = enode_binop($1,$3,NODE_L_AND); }
       | expr SHLEFT expr { $$ = enode_binop($1,$3,NODE_SHL); }
       | expr SHRIGHT expr { $$ = enode_binop($1,$3,NODE_SHR); }
       | '(' expr ')' { $$ = $2; }
| '<' expr ',' expr ',' expr ',' expr '>' // FIXME - horribly broken
| '<' expr ',' expr ',' expr '>' // FIXME
| '[' list ']' // FIXME
| expr INCR  /* FIXME */
| expr DECR  /* FIXME */
| INCR expr { $$ = $2; }  /* FIXME */
| DECR expr { $$ = $2; }  /* FIXME */
| '!' expr { $$ = enode_monop($2, NODE_L_NOT); } 
| '~' expr { $$ = enode_monop($2, NODE_NOT); }
| '-' expr { $$ = enode_negate($2); } 
| '(' type ')' expr %prec CAST { $$ = $4 } /* FIXME */ /* FIXME - operator precidence? */ /* FIXME - shift/reduce conflicts */
       
   ;
type : INTEGER { $$ = VM_TYPE_INT; } 
     | FLOAT { $$ = VM_TYPE_FLOAT; } 
     | STRING { $$ = VM_TYPE_STR; } 
     | KEY { $$ = VM_TYPE_KEY; } 
     | VECTOR { $$ = VM_TYPE_VECT; } 
     | ROTATION { $$ = VM_TYPE_ROT; } 
     | LIST { $$ = VM_TYPE_NONE; /* FIXME FIXME */ } 
  ;
%%
#include <stdio.h>

		/* bison -d lsl.y && flex lsl.lex && gcc -o lsl_compile lsl.tab.c lex.yy.c -lfl */

static void print_expr(expr_node *enode) {
  switch(enode->node_type) {
  case NODE_CONST:
    switch(enode->vtype) {
    case VM_TYPE_INT:
      printf("%i ", enode->u.i); break;
    case VM_TYPE_FLOAT:
      printf("%f ", (double)enode->u.f); break;
    default:
      printf("<unknown const> "); break;
    }
    break;
  case NODE_IDENT:
    printf("%s ", enode->u.s); break;
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
  }
}

int main(int argc, char** argv) {
  extern FILE *yyin;
   yyin = fopen(argv[1],"r");
   yydebug = 1;
   //errors = 0;
#if 1
   global_prog.func = NULL;
   yyparse();
   if(global_prog.func != NULL) {
     statement* statem; func_arg *arg;
     printf("%s %s(", type_names[global_prog.func->ret_type],
	    global_prog.func->name);
     for(arg = global_prog.func->args; arg != NULL; arg = arg->next) {
       printf("%s %s, ", type_names[arg->vtype], arg->name);
     }
     printf(") {\n");
     statem = global_prog.func->code->first;
     for( ; statem != NULL; statem = statem->next) {
       printf("  statement ");
       print_expr(statem->expr);
       printf("\n");
     }
     printf("}\n");
   }
#else
   int i;
   for(;;) {
     i = yylex(); printf(" %i ", i);
     if(i == 0) break;
   }
#endif
   
   return 0;
}

void yyerror(char const *error) { 
  printf("Line %i: %s\n", yylloc.first_line, error);
}

