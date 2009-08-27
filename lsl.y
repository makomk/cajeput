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
#define STMT_WHILE 4
#define STMT_DO 5
#define STMT_FOR 6
#define STMT_DECL 7 /* variable declaration */

 typedef struct statement {
   int stype; /* STMT_* */
   expr_node *expr[3];
   struct statement *child[2];
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
#define NODE_ASSIGNMOD 26
 /* mono ops */
#define NODE_NEGATE 27
#define NODE_NOT 28 /* ~ - bitwise not */
#define NODE_L_NOT 29 /* ! - logical not */
#define NODE_PREINC 30 /* ++foo */
#define NODE_POSTINC 31 /* foo++ */
#define NODE_PREDEC 32 /* --foo */
#define NODE_POSTDEC 33 /* foo-- */

#define NODE_CALL 34 /* procedure call */

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

/* FIXME - need to remove quotes either here or in lexer */
static expr_node * enode_make_str(char *s) {
  struct expr_node *enode = malloc(sizeof(struct expr_node));
  enode->u.s = s;
  enode->node_type = NODE_CONST;
  enode->vtype = VM_TYPE_STR;
  return enode;
}


static expr_node * enode_make_id(char *s) {
   struct expr_node *enode = malloc(sizeof(struct expr_node));
   enode->node_type = NODE_IDENT;
   enode->u.s = s;
    return enode;
}

 static expr_node * enode_make_id_type(char *s, uint8_t vtype) {
   struct expr_node *enode = malloc(sizeof(struct expr_node));
   enode->node_type = NODE_IDENT; enode->vtype = vtype;
   enode->u.s = s;
   return enode;
}

 static expr_node *enode_make_call(void) { // FIXME!!!!!!!
   struct expr_node *enode = malloc(sizeof(struct expr_node));
   enode->node_type = NODE_CALL;
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

 static statement* new_statement(void) {
  statement *statem = malloc(sizeof(statement));
  statem->next = NULL; 
  return statem;
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
%token IF ELSE WHILE FOR DO STATE DEFAULT RETURN JUMP
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
%type <enode> expr num_const variable call
%type <bblock> statements function_body
%type <stat> statement if_stmt while_stmt do_stmt for_stmt ret_stmt local 
%type <stat> jump_stmt label_stmt
%type <func> function program functions
%type <arg> arguments arglist argument
%type <vtype> type 
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
| statements statement { 
  if($2 != NULL) { *($1->add_here) = $2; $1->add_here = &$2->next; }
  $$ = $1;
 } ;
statement : ';' { $$ = NULL } |  expr ';' { 
  $$ = new_statement();
  $$->stype = STMT_EXPR; $$->expr[0] = $1; 
 } 
            | if_stmt ; // FIXME 
            | do_stmt 
	    | while_stmt
	    | for_stmt
	    | ret_stmt ';' ; // FIXME
            | local ';' ;
            | jump_stmt ;
            | label_stmt ;
	    | '{' statements '}' ;
local : type IDENTIFIER {
  $$ = new_statement(); $$->stype = STMT_DECL; 
  $$->expr[0] = enode_make_id_type($2,$1); $$->expr[1] = NULL;
 }
    | type IDENTIFIER '=' expr {
  $$ = new_statement(); $$->stype = STMT_DECL; 
  $$->expr[0] = enode_make_id_type($2,$1); $$->expr[1] = $4;
 }; 
if_stmt : IF '(' expr ')' '{' statements '}' {
  $$ = new_statement(); $$->stype = STMT_IF; $$->expr[0] = $3;
  $$->child[0] = NULL; $$->child[1] = NULL; /* FIXME */
 }
        | IF '(' expr ')' '{' statements '}' ELSE '{' statements '}';
while_stmt : WHILE '(' expr ')' statement {
  $$ = new_statement(); $$->stype = STMT_WHILE; 
  $$->expr[0] = $3; $$->child[0] = $5;
 };
do_stmt : DO statement WHILE '(' expr ')' {
  $$ = new_statement(); $$->stype = STMT_DO; 
  $$->child[0] = $2; $$->expr[0] = $5;
 };
for_stmt : FOR '(' opt_expr ';' opt_expr ';' opt_expr ')'  statement ;
jump_stmt : JUMP IDENTIFIER ';';
label_stmt : '@' IDENTIFIER ';';
opt_expr : | expr ;
ret_stmt : RETURN | RETURN expr ;
variable: IDENTIFIER { $$ = enode_make_id($1); }
| IDENTIFIER '.' IDENTIFIER { $$ = enode_make_id($1); } /* FIXME - handle .x right */
   ; 
call_args : /* nowt */ | expr | call_args ',' expr  ;
call : IDENTIFIER '(' call_args ')' { $$ = enode_make_call(); } // FIXME!!!!
list : | expr | list ',' expr ; // FIXME. Also, merge with argument list?
num_const : NUMBER { $$ = enode_make_int($1); }
       | REAL { $$ = enode_make_float($1); }
       | IDENTIFIER  { $$ = enode_make_id($1); }
       | '-' num_const { $$ = enode_negate($2); } ;
expr : NUMBER { $$ = enode_make_int($1); }
       | REAL { $$ = enode_make_float($1); }
       | STR { $$ = enode_make_str($1); }
       | call { $$ = $1; } 
       | variable { $$ = $1; }
       | variable '=' expr { $$ = enode_binop($1,$3,NODE_ASSIGN); }
       | variable ASSIGNADD expr { $$ = enode_binop($1,$3,NODE_ASSIGNADD); }
       | variable ASSIGNSUB expr { $$ = enode_binop($1,$3,NODE_ASSIGNSUB); }
       | variable ASSIGNMUL expr { $$ = enode_binop($1,$3,NODE_ASSIGNMUL); }
       | variable ASSIGNDIV expr { $$ = enode_binop($1,$3,NODE_ASSIGNDIV); }
       | variable ASSIGNMOD expr { $$ = enode_binop($1,$3,NODE_ASSIGNMOD); }
       | expr '+' expr { $$ = enode_binop($1,$3,NODE_ADD); }
       | expr '-' expr { $$ = enode_binop($1,$3,NODE_SUB); }
       | expr '*' expr { $$ = enode_binop($1,$3,NODE_MUL); }
       | expr '/' expr { $$ = enode_binop($1,$3,NODE_DIV); }
       | expr '%' expr { $$ = enode_binop($1,$3,NODE_MOD); }
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
| '<' num_const ',' num_const  ',' num_const ',' num_const '>' // FIXME - horribly broken
| '<' num_const ',' num_const  ',' num_const '>' // FIXME
| '[' list ']' // FIXME
| expr INCR { $$ = enode_monop($1, NODE_POSTINC); } 
| expr DECR { $$ = enode_monop($1, NODE_POSTDEC); } 
| INCR expr { $$ = enode_monop($2, NODE_PREINC); }
| DECR expr { $$ = enode_monop($2, NODE_PREDEC); } 
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
    case VM_TYPE_STR:
      printf("\"%s\" ", enode->u.s); break;
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
  default:
    printf("<unknown expr %i>",enode->node_type);
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
       switch(statem->stype) {
       case STMT_EXPR:
	 print_expr(statem->expr[0]);
	 break;
       default:
	 printf("<unknown statement type %i>", statem->stype);
	 break;
       }
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

