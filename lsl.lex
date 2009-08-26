%{
#include "lsl.tab.h"
#include <string.h>
%}
%%
[0-9]+ { yylval.str = strdup(yytext); return NUMBER; }
[a-zA-Z][a-zA-Z0-9_]* { yylval.str = strdup(yytext); return IDENTIFIER; }
[ \t\n\r]+
. { return yytext[0]; }
