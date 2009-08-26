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
key { return KEY; }
vector { return VECTOR; }
rotation { return ROTATION; }
if { return IF; }
else { return ELSE; }
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

[0-9]+[.][0-9]* { yylloc.first_line = yylineno; yylval.str = strdup(yytext); return REAL; } /* FIXME - handle exponent */
[0-9]+ {yylloc.first_line = yylineno; yylval.str = strdup(yytext); return NUMBER; }
[a-zA-Z][a-zA-Z0-9_]* {yylloc.first_line = yylineno; yylval.str = strdup(yytext); return IDENTIFIER; }
\"[^\n\r\"]*\" {yylloc.first_line = yylineno; yylval.str = strdup(yytext); return STR; }
[ \t\n\r]+
\/\/[^\n\r]*
. { yylloc.first_line = yylineno; return yytext[0]; }
