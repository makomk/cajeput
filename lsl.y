%{
#include <math.h>
#include <stdlib.h>
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


#define NODE_INT 1
#define NODE_IDENT 2
#define NODE_ADD 3
#define NODE_SUB 4
#define NODE_MUL 5
#define NODE_DIV 6

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

 struct basic_block *global_bblock; // FIXME - remove this

%}
/* %debug */
%error-verbose
%union {
  struct expr_node *enode;
  struct statement *stat;
  struct basic_block *bblock;
  char *str;
}
%token IF ELSE WHILE FOR
%token <str> IDENTIFIER
%token <str> NUMBER
%token INTEGER FLOAT STRING KEY VECTOR ROTATION /* LSL types */
%left '-' '+'
%left '*' '/'
%type <enode> expr statement;
%type <bblock> statements function_body;
%type <bblock> functions program
%%
program : globals functions states { $$ = $2; global_bblock = $2; }; 
globals : ;
functions : ret_type IDENTIFIER '(' arguments ')' function_body { $$ = $6; } ;
states : ;
arguments : /* nothing */ | arglist ;
arglist : argument | arglist ',' argument ;
argument: type IDENTIFIER ;
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
ret_type : /* nothing */ | type ;
type : INTEGER | FLOAT | STRING | KEY | VECTOR | ROTATION;
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
   global_bblock = NULL;
   yyparse();
   if(global_bblock != NULL) {
     statement* statem = global_bblock->first;
     for( ; statem != NULL; statem = statem->next) {
       printf("statement ");
       print_expr(statem->expr);
       printf("\n");
     }
   }
#else
   int i;
   for(;;) {
     i = yylex(); printf(" %i ", i);
     if(i == 0) break;
   }
#endif
}

void yyerror(char const *error) { 
   printf("%s\n", error);
}

