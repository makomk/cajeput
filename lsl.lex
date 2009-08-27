%{
#include <string.h>
#include <stdint.h>
#include "lsl.tab.h"
%}
%option   yylineno
%%
state {yylloc.first_line = yylineno; return STATE; }
default {yylloc.first_line = yylineno;return DEFAULT; }
integer {yylloc.first_line = yylineno; return INTEGER; }
float {yylloc.first_line = yylineno; return FLOAT; }
string {yylloc.first_line = yylineno; return STRING; }
key { yylloc.first_line = yylineno; return KEY; }
list { yylloc.first_line = yylineno; return LIST; }
vector { yylloc.first_line = yylineno; return VECTOR; }
quaternion {yylloc.first_line = yylineno; return ROTATION; }
rotation {yylloc.first_line = yylineno; return ROTATION; }
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

[0-9]+[.][0-9]* { yylloc.first_line = yylineno; yylval.str = strdup(yytext); return REAL; } /* FIXME - handle exponent */
(0x)?[0-9]+ {yylloc.first_line = yylineno; yylval.str = strdup(yytext); return NUMBER; }
[a-zA-Z][a-zA-Z0-9_]* {yylloc.first_line = yylineno; yylval.str = strdup(yytext); return IDENTIFIER; }
\"([^\n\r\"\\]|\\.)*\" { 
  int slen = strlen(yytext); 
  yylloc.first_line = yylineno; 
  yylval.str = malloc(slen-1); memcpy(yylval.str, yytext+1, slen-2); 
  yylval.str[slen-2] = 0; return STR;
 }
[ \t\n\r]+
\/\/[^\n\r]*
. { yylloc.first_line = yylineno; return yytext[0]; }
