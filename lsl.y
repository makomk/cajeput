%{
#include <math.h>
#include <stdlib.h>
#include <stdint.h>
int yylex (void);
void yyerror (char const *);
int yydebug;

typedef struct expr_node {
  int node_type;
  union {
    struct expr_node* child[2];
    int i; char* s;
  } u;
} expr_node;

 typedef struct statement {
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

#define NODE_INT 1
#define NODE_IDENT 2
#define NODE_ADD 3
#define NODE_SUB 4
#define NODE_MUL 5
#define NODE_DIV 6

 /* FIXME - types should be in a shared header */
#define VM_TYPE_NONE  0
#define VM_TYPE_INT   1
#define VM_TYPE_FLOAT 2
#define VM_TYPE_STR   3
#define VM_TYPE_KEY   4
#define VM_TYPE_VECT  5
#define VM_TYPE_ROT   6

 const char* type_names[] = {"void","int","float","str","key","vect","rot"};

struct expr_node * enode_make_num(int i) {
   struct expr_node *enode = malloc(sizeof(struct expr_node));
   enode->node_type = NODE_INT;
   enode->u.i = i;
    return enode;
}

struct expr_node * enode_make_id(char *s) {
   struct expr_node *enode = malloc(sizeof(struct expr_node));
   enode->node_type = NODE_IDENT;
   enode->u.s = s;
    return enode;
}

 expr_node * enode_binop(expr_node *l, expr_node *r, int node_type) {
    expr_node *enode = malloc(sizeof(expr_node));
    enode->node_type = node_type;
    enode->u.child[0] = l; enode->u.child[1] = r; 
    return enode;
}

 struct lsl_program global_prog; // FIXME - remove this

%}
/* %debug */
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
%token IF ELSE WHILE FOR STATE DEFAULT
%token <str> IDENTIFIER
%token <str> NUMBER
%token <vtype> INTEGER FLOAT STRING KEY VECTOR ROTATION /* LSL types */
%left '-' '+'
%left '*' '/'
%type <str> state_id;
%type <enode> expr statement;
%type <bblock> statements function_body;
%type <func> function program
%type <arg> arguments arglist argument
%type <vtype> type ret_type
%%
program : globals function states { $$ = $2; global_prog.func = $2; }; 
globals : ;
function : ret_type IDENTIFIER '(' arguments ')' function_body {
  $$ = malloc(sizeof(function));
  $$->ret_type = $1;
  $$->name = $2; $$->args = $4; $$->code = $6;
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
| statements statement ';' { 
  statement *statem = malloc(sizeof(statement));
  statem->expr = $2; statem->next = NULL;
  *($1->add_here) = statem; $1->add_here = &statem->next;
  $$ = $1;
} ;
statement : /*nothing*/ { $$ = NULL; }
        | expr { $$ = $1; } ;
expr : NUMBER { $$ = enode_make_num(atoi($1)); }
       | IDENTIFIER { $$ = enode_make_id($1); }
       | expr '+' expr { $$ = enode_binop($1,$3,NODE_ADD); }
       | expr '-' expr { $$ = enode_binop($1,$3,NODE_SUB); }
       | expr '*' expr { $$ = enode_binop($1,$3,NODE_MUL); }
       | expr '/' expr { $$ = enode_binop($1,$3,NODE_DIV); }
       | '(' expr ')' { $$ = $2; }
   ;
ret_type : /* nothing */ { $$ = VM_TYPE_NONE; } | type ;
type : INTEGER { $$ = VM_TYPE_INT; } 
     | FLOAT { $$ = VM_TYPE_FLOAT; } 
     | STRING { $$ = VM_TYPE_STR; } 
     | KEY { $$ = VM_TYPE_KEY; } 
     | VECTOR { $$ = VM_TYPE_VECT; } 
     | ROTATION { $$ = VM_TYPE_ROT; } 
  ;
%%
#include <stdio.h>

		/* bison -d lsl.y && flex lsl.lex && gcc -o lsl_compile lsl.tab.c lex.yy.c -lfl */

static void print_expr(expr_node *enode) {
  switch(enode->node_type) {
  case NODE_INT:
    printf("%i ", enode->u.i); break;
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
   printf("%s\n", error);
}

