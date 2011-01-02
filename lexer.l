%{

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#include "llvm/Value.h"
#include "parse_helpers.h"
#include "parser.h"

extern "C" int yylex();
extern "C" void yyerror( char *s );
extern "C" int yyparse();
void convertConstant( int base, const char *name );

int lineno = 1;

#define DEBUG_PRINT printf

%}


%%

return						return KEYWORD_RETURN;
if							return KEYWORD_IF;
else						return KEYWORD_ELSE;
for							return KEYWORD_FOR;
while						return KEYWORD_WHILE;

char|short|int|long|float|double|string	yylval.string = strdup( yytext ); return BUILTIN_TYPE;
void 						yylval.string = strdup( yytext ); return VOID;

signed|unsigned				yylval.string = strdup( yytext ); return BUILTIN_TYPE_MODIFIER;

const|volatile

auto|register|static|extern


=|\*=|\/=|\+=|\-=|\<\<=|\>\>=|\|=|\&=			yylval.string = strdup( yytext ); return ASSIGN;

\|\|						yylval.string = strdup( yytext ); return LOR; 
\&\&						yylval.string = strdup( yytext ); return LAND;
==|\!=						yylval.string = strdup( yytext ); return EQ;
\<|\>|\<=|\>=				yylval.string = strdup( yytext ); return COMP;
\<\<|\>\>					yylval.string = strdup( yytext ); return SHIFT;

\"[^\"]*\"					yylval.string = strdup( yytext ); return CONSTANT_STRING;

\/\/[^\n]*					DEBUG_PRINT( "comment is %s\n", yytext );

[ \t]	
\n							lineno++;

[-+]{0,1}[1-9][0-9]*[Ll]{0,2}	convertConstant( 10, yytext ); return CONSTANT_NUMBER;
0[0-7]*[Ll]{0,2}			convertConstant( 8, yytext ); return CONSTANT_NUMBER;
0x[0-9a-fA-F]*[Ll]{0,2}		convertConstant( 16, yytext ); return CONSTANT_NUMBER;
\'(.|\\.|\\0[0-7]{3}|\\0x[0-9a-fA-F]{2})\'	convertConstant( -1, yytext ); return CONSTANT_CHAR;

[a-zA-Z][a-zA-Z_0-9]* 		DEBUG_PRINT( "Symbol %s\n", yytext ); yylval.string = strdup( yytext ); return SYMBOL;

\(|\)|\{|\}|\+|\-|\*|\^|\/|\%|\&|\||;|,		return yytext[0];
.							DEBUG_PRINT( "unknown char %s\n", yytext ); return yytext[0];

%%

void convertConstant( int base, const char *name )
{
	char *end = NULL;	

	if ( base == 10 && name[ 0 ] == '-' )
	{
		yylval.constant_date._signed = true;
		yylval.constant_date.value = strtoll( name + 1, &end, base );
		if ( end[ 0 ] == 'l' || end[ 0 ] == 'L' )
			yylval.constant_date.width = 64;
		else
			yylval.constant_date.width = 32;

		printf( "ContantS %s->%lld\n", name, yylval.constant_date.value );
	}
	else
	{
		yylval.constant_date._signed = false;
		if ( base == -1 )
		{
			assert( name[ 0 ] == '\'' );
			if ( name[ 1 ] == '\\' )
			{
				if ( name[ 2 ] == '\\' || name[ 2 ] == '\'' )
				{
					yylval.constant_date.value = name[ 2 ];
					assert( end[ 3 ] == '\'' );
				}
				else
				{
					yylval.constant_date.value = strtoll( name + 2, &end, 8 );
					assert( end[ 0 ] == '\'' );
				}
			}
			else
			{
				yylval.constant_date.value = name[ 1 ];
				assert( name[ 2 ] == '\'' );
			}
				
			yylval.constant_date.width = 8;			
			printf( "Contant8 %s->%lld\n", name, yylval.constant_date.value );
		}
		else
		{
			yylval.constant_date.uvalue = strtoll( name, &end, base );
			if ( end[ 0 ] == 'l' || end[ 0 ] == 'L' )
				yylval.constant_date.width = 64;
			else
				yylval.constant_date.width = 32;

			printf( "ContantU %s->%lld\n", name, yylval.constant_date.uvalue );
		}
	}
	
}

void yyerror( char *s )
{
	printf( "%s at %s in line %d\n", s, yytext, lineno );
}

void usage( char *name )
{
	printf( "%s filename.asm\n", name );
	exit( -1 );
}

extern int yydebug;

#include "parse_helpers.h"

int main( int argc, char *argv[] )
{
	if ( argc > 2 || argc < 2 )
		usage( argv[ 0 ] );

	yyin = fopen( argv[ 1 ], "r" );

#if ( YYDEBUG != 0 )
	yydebug = 1;
#endif

	init( argv[ 1 ] );
	
	yyparse();
	
	destroy();
	
	//while ( yylex() != 0 );
	
	return 0;
}

