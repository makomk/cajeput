%{
#include <string.h>
#include <stdint.h>
#include <assert.h>

#include "caj_lsl_parse.h"
typedef struct lsl_location YYLTYPE;
# define YYLTYPE_IS_DECLARED 1

#include "lsl.tab.h"

# define YY_USER_ACTION  do {				\
  yylloc.first_column = yylloc.last_column;		\
  yylloc.last_column += yyleng;				\
  } while(0);

static void unescape_str(char *str) {
  int i = 0, j = 0;
  for( ; str[i] != 0; i++,j++) {
    if(str[i] != '\\') {
      str[j] = str[i];
    } else {
      switch(str[++i]) {
      case 0:
	assert(0); abort(); str[j] = 0; /* should never happen */
	return;
      case '\\':
	str[j] = '\\'; break;
      case 'n':
	str[j] = '\n'; break;
      case 'r':
	str[j] = '\r'; break;
	/* FIXME - handle '\t' (requires special code; expands to 4 spaces. */
      case '"':
      default:
	str[j] = str[i]; break;
      }
    }
  }
  str[j] = 0;
}

%}
%option yylineno batch 8bit noyywrap nounput noinput
%x comment
%%
<*>\n                        { 
  yylloc.first_line++; yylloc.last_line++;
  yylloc.first_column = 0; yylloc.last_column = 0;
 }
\/\* BEGIN(comment);
<comment>\*\/        BEGIN(INITIAL);
<comment>[^*\n]+ /* this is subtle and quick to break. */
<comment>\*+ /* have to make * start of symbol so end-comment rule works */
state {return STATE; }
default {return DEFAULT; }
integer {return INTEGER; }
float {return FLOAT; }
string {return STRING; }
key { return KEY; }
list { return LIST; }
vector { return VECTOR; }
quaternion { return ROTATION; }
rotation { return ROTATION; }
return { return RETURN; }
if { return IF; }
else { return ELSE; }
while { return WHILE; }
for { return FOR; }
do { return DO; }
jump { return JUMP; }
\+\+ { return INCR; }
-- { return DECR; }
\<\< { return SHLEFT; }
\>\> { return SHRIGHT; }
&& { return L_AND; }
\|\| { return L_OR; }
== { return EQUAL; }
!= { return NEQUAL; }
\<= { return LEQUAL; }
\>= { return GEQUAL; }
\+= { return ASSIGNADD; }
-= { return ASSIGNSUB; }
\*= { return ASSIGNMUL; }
\/= { return ASSIGNDIV; }
%= { return ASSIGNMOD; }

[0-9]+[.][0-9]* { yylval.str = strdup(yytext); return REAL; } /* FIXME - handle exponent */
(0x)?[0-9]+ { yylval.str = strdup(yytext); return NUMBER; }
[a-zA-Z][a-zA-Z0-9_]* { yylval.str = strdup(yytext); return IDENTIFIER; }
\"([^\n\r\"\\]|\\.)*\" { 
  int slen; unescape_str(yytext); slen = strlen(yytext); 
  yylval.str = malloc(slen-1); memcpy(yylval.str, yytext+1, slen-2); 
  yylval.str[slen-2] = 0; return STR;
 }
[ \t\r]+
\/\/[^\n\r]*
[^\n] { return yytext[0]; }
