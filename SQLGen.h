#ifndef BLANG_SQLGEN_H_
#define BLANG_SQLGEN_H_

#include <string>
#include <vector>
#include "Type.h"
#include "Expression.h"

namespace QLang
{

// Generated SQL with parameterized placeholders
struct SQLStatement
{
	std::string sql;
	std::vector<std::string> params;
};

// Walks query AST nodes and emits parameterized SQL strings.
class SQLGen
{
public:
	// Generate SELECT from a QueryExpression
	static SQLStatement generateSelect( QueryExpression *query, Module *mod );

	// Generate INSERT from an InsertExpression
	static SQLStatement generateInsert( InsertExpression *insert, Module *mod );

	// Generate UPDATE from an UpdateExpression
	static SQLStatement generateUpdate( UpdateExpression *update, Module *mod );

	// Generate DELETE from a DeleteExpression
	static SQLStatement generateDelete( DeleteExpression *del, Module *mod );

	// Generate CREATE TABLE from a table struct definition
	static std::string generateCreateTable( StructDefinition *structDef );

	// Generate ALTER TABLE ADD COLUMN
	static std::string generateAddColumn( const std::string &tableName,
		const std::string &columnName, const std::string &columnType );

	// Map BLang type to SQL type
	static std::string blangTypeToSQL( const std::string &blangType );

private:
	// Convert a table struct name to a SQL table name (lowercase)
	static std::string tableNameToSQL( const std::string &structName );

	// Generate a WHERE clause expression as SQL
	static std::string exprToSQL( const Expression *expr, std::vector<std::string> &params );
};

} // namespace QLang

#endif // BLANG_SQLGEN_H_
