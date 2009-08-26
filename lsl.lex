%{
#include <string.h>
#include <stdint.h>
#include "lsl.tab.h"
%}
%%
state {return STATE; }
default {return DEFAULT; }
integer { return INTEGER; }
float { return FLOAT; }
string { return STRING; }
key { return KEY; }
vector { return VECTOR; }
rotation { return ROTATION; }
[0-9]+ { yylval.str = strdup(yytext); return NUMBER; }
[a-zA-Z][a-zA-Z0-9_]* { yylval.str = strdup(yytext); return IDENTIFIER; }
[ \t\n\r]+
[/][/][^\n\r]*
. { return yytext[0]; }
