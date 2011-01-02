%{

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include "parse_helpers.h"
#include "llvm/Value.h"
#include <string>

extern "C" int yylex();
extern "C" void yyerror( char *s );
extern "C" int yyparse();


%}

%union {
	const char *string;
	ConstData constant_date;
	llvm::Value *value;
}

%token <string> SYMBOL
%token <string> CONSTANT_STRING
%token <string> BUILTIN_TYPE
%token <constant_date> CONSTANT_CHAR
%token <constant_date> CONSTANT_NUMBER
%token <string> ASSIGN
%token <string> VOID

%token <string> LOR LAND EQ COMP SHIFT
%token KEYWORD_RETURN KEYWORD_IF KEYWORD_ELSE KEYWORD_FOR KEYWORD_WHILE
%token BUILTIN_TYPE_MODIFIER

%type <string> type_name type_name_or_void function_name function_decl param_decl
%type <value> variable_decl variable_name lvalue rvalue call constant array_index

%type <value> expression expression_logic_AND expression_OR expression_XOR
%type <value> expression_AND expression_EQ expression_COMP expression_SHIFT
%type <value> expression_ADD expression_MULT expression_cast assignment

%%

file: file function_definition
	| function_definition
	;

	
function_definition: func_type_and_name func_params func_block { endFunction(); }
	;	

func_type_and_name: type_name_or_void function_decl { startFunction( $1, $2 ); }
	;

func_params: '(' decl_parameters ')' { endFunctionDef(); }
	;

decl_parameters: decl_parameters ',' decl_parameter
	| decl_parameter
	|
	;

decl_parameter: type_name param_decl { addFunctionParam( $1, $2 ); }
	;

	
statements: statement
	| statements statement
	|
	;
	
statement: assignment ';'
	| declaration ';'
	| if_statement { printf( "if\n" ); } 
	| KEYWORD_WHILE '(' expression ')' statement
	| KEYWORD_FOR '(' declaration ';' expression ';' expression ')' statement
	| KEYWORD_RETURN expression ';'		{ addReturn( $2 ); }
	| KEYWORD_RETURN ';'				{ addReturn( NULL ); }
	| anon_block 						{ printf( "block\n" ); }
	| ';'
	;

if_clause: KEYWORD_IF '(' expression ')' { startIf( $3 ); }
	;
	
else_clause: KEYWORD_ELSE '(' expression ')' { startElse( $3 ); }
	;

else_start: KEYWORD_ELSE				{ startElse( NULL ); }
	;

if_statement: if_clause statement					{ endIf(); }
	| if_clause '{' statements '}'					{ endIf(); }
	| if_clause statements  else_statement			{ endIf(); }
	| if_clause '{' statements '}' else_statement	{ endIf(); }
	;
	
else_statement: else_clause statement
	| else_clause '{' statements '}'
	| else_clause statement else_statement
	| else_clause '{' statements '}' else_statement
	| else_start statement
	| else_start '{' statements '}'
	;
	
anon_block: start_block statements end_block;

func_block: func_start_block statements end_block;

func_start_block: '{'
	;
	
start_block: '{'		{ pushScope( SCOPE_ANONYMOUS, "" ); }
	;
	
end_block: '}'			{ popScope(); }
	;

assignment: lvalue ASSIGN expression			{ $$ = getAssignExpression( $2, $1, $3 ); }
	| expression
	;	
	
declaration: decl_type_name variable_decl_and_assigns 

variable_decl_and_assigns: variable_decl_and_assigns ',' variable_decl_and_assign
	| variable_decl_and_assign
	;
	
variable_decl_and_assign: variable_decl ASSIGN expression { getAssignExpression( $2, $1, $3 ); }
	| variable_decl
	;

decl_type_name : type_name { getType( $1 ); }
	;
	
expression: expression_logic_AND LOR expression		{ $$ = getBinaryExpression( $2, $1, $3 ); }
	| expression_logic_AND							{ $$ = $1; }
	;

expression_logic_AND: expression_OR LAND expression_logic_AND	{ $$ = getBinaryExpression( $2, $1, $3 ); }
	| expression_OR												{ $$ = $1; }
	;
	
expression_OR: expression_XOR '|' expression_OR		{ $$ = getBinaryExpression( "|", $1, $3 ); }
	| expression_XOR								{ $$ = $1; }
	;
	
expression_XOR: expression_AND '^' expression_XOR	{ $$ = getBinaryExpression( "^", $1, $3 ); }
	| expression_AND								{ $$ = $1; }
	;
	
expression_AND: expression_EQ '&' expression_AND	{ $$ = getBinaryExpression( "&", $1, $3 ); }
	| expression_EQ									{ $$ = $1; }
	;
	
expression_EQ: expression_COMP EQ expression_EQ		{ $$ = getBinaryExpression( $2, $1, $3 ); }
	| expression_COMP								{ $$ = $1; }
	;

expression_COMP: expression_SHIFT COMP expression_COMP	{ $$ = getBinaryExpression( $2, $1, $3 ); }
	| expression_SHIFT									{ $$ = $1; }
	;
	
expression_SHIFT: expression_ADD SHIFT expression_SHIFT	{ $$ = getBinaryExpression( $2, $1, $3 ); }
	| expression_ADD									{ $$ = $1; }
	;
	
expression_ADD: expression_MULT '+' expression_ADD		{ $$ = getBinaryExpression( "+", $1, $3 ); }
	| expression_MULT '-' expression_ADD				{ $$ = getBinaryExpression( "-", $1, $3 ); }
	| expression_MULT									{ $$ = $1; }
	;
	
expression_MULT: expression_cast '*' expression_MULT	{ $$ = getBinaryExpression( "*", $1, $3 ); }
	| expression_cast '/' expression_MULT				{ $$ = getBinaryExpression( "/", $1, $3 ); }
	| expression_cast '%' expression_MULT				{ $$ = getBinaryExpression( "%", $1, $3 ); }
	| expression_cast									{ $$ = $1; }
	;

expression_cast: '(' type_name ')' expression_cast	{ $$ = $4 }
	| '(' type_name ')' rvalue						{ $$ = $4 }
	| rvalue 										{ $$ = $1 }
	;
	
lvalue: variable_name { $$ = $1; }
	| array_index { $$ = $1; }
	;

rvalue: variable_name { $$ = $1; }
	| constant { $$ = $1; }
	| call { $$ = $1; }
	| array_index { $$ = $1; }
	;

array_index: variable_name '[' expression ']'	{ $$ = getArrayLookupValue( $1, $3 ); }
	;

parameters: parameter
	| parameter ',' parameters
	;

parameter:	expression	{ addFunctionCallParam( $1 ); }
	;

call: function_name '(' parameters ')' { $$ = endFunctionCall( $1 ); }

constant: CONSTANT_NUMBER 	{ $$ = getConstantNumberValue( &$1 ); }
	| CONSTANT_STRING		{ $$ = getConstantStringValue( $1 ); }
	| CONSTANT_CHAR			{ $$ = getConstantNumberValue( &$1 ); }
	;
	
type_name_or_void: type_name | VOID { $$ = $1; }

type_name: BUILTIN_TYPE		{ $$ = $1; }

function_name: SYMBOL		{ startFunctionCall(); $$ = $1 }

function_decl: SYMBOL 		{ $$ = $1; }

variable_name: SYMBOL		{ $$ = getVariableValue( $1 ) }

variable_decl: SYMBOL		{ $$ = declareVariable( $1 ) }

param_decl: SYMBOL			{ $$ = $1 }
