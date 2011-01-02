/* A Bison parser, made by GNU Bison 2.3.  */

/* Skeleton interface for Bison's Yacc-like parsers in C

   Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* Tokens.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
   /* Put the tokens into the symbol table, so that GDB and other debuggers
      know about them.  */
   enum yytokentype {
     SYMBOL = 258,
     CONSTANT_STRING = 259,
     BUILTIN_TYPE = 260,
     CONSTANT_CHAR = 261,
     CONSTANT_NUMBER = 262,
     ASSIGN = 263,
     VOID = 264,
     LOR = 265,
     LAND = 266,
     EQ = 267,
     COMP = 268,
     SHIFT = 269,
     KEYWORD_RETURN = 270,
     KEYWORD_IF = 271,
     KEYWORD_ELSE = 272,
     KEYWORD_FOR = 273,
     KEYWORD_WHILE = 274,
     BUILTIN_TYPE_MODIFIER = 275
   };
#endif
/* Tokens.  */
#define SYMBOL 258
#define CONSTANT_STRING 259
#define BUILTIN_TYPE 260
#define CONSTANT_CHAR 261
#define CONSTANT_NUMBER 262
#define ASSIGN 263
#define VOID 264
#define LOR 265
#define LAND 266
#define EQ 267
#define COMP 268
#define SHIFT 269
#define KEYWORD_RETURN 270
#define KEYWORD_IF 271
#define KEYWORD_ELSE 272
#define KEYWORD_FOR 273
#define KEYWORD_WHILE 274
#define BUILTIN_TYPE_MODIFIER 275




#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE
#line 19 "parser.yy"
{
	const char *string;
	ConstData constant_date;
	llvm::Value *value;
}
/* Line 1529 of yacc.c.  */
#line 95 "parser.h"
	YYSTYPE;
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif

extern YYSTYPE yylval;

