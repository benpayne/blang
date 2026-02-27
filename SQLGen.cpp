#include "SQLGen.h"

#include <algorithm>
#include <cctype>
#include <sstream>

using namespace QLang;
using namespace std;

string SQLGen::tableNameToSQL( const string &structName )
{
	// Convert PascalCase to lowercase: User -> user, UserPost -> user_post
	string result;
	for ( size_t i = 0; i < structName.size(); i++ )
	{
		char c = structName[i];
		if ( isupper( c ) )
		{
			if ( i > 0 ) result += '_';
			result += (char)tolower( c );
		}
		else
		{
			result += c;
		}
	}
	return result;
}

string SQLGen::blangTypeToSQL( const string &blangType )
{
	if ( blangType == "int" || blangType == "long" || blangType == "short" )
		return "INTEGER";
	if ( blangType == "float" || blangType == "double" )
		return "REAL";
	if ( blangType == "string" )
		return "TEXT";
	if ( blangType == "bool" )
		return "INTEGER"; // SQLite stores bools as integers
	if ( blangType == "char" )
		return "TEXT";
	return "TEXT"; // default
}

string SQLGen::exprToSQL( const Expression *expr, vector<string> &params )
{
	// Handle query field references: .field -> table.field
	const QueryFieldExpression *field = dynamic_cast<const QueryFieldExpression*>( expr );
	if ( field )
		return field->getFieldName();

	// Handle binary operations: a == b -> a = b, a != b -> a <> b
	const OperationsExpression *ops = dynamic_cast<const OperationsExpression*>( expr );
	if ( ops )
	{
		string left = exprToSQL( ops->mOp1, params );
		string right = exprToSQL( ops->mOp2, params );
		string op = ops->mOperation;

		// Map BLang operators to SQL operators
		if ( op == "==" ) op = "=";
		else if ( op == "!=" ) op = "<>";
		else if ( op == "&&" ) op = "AND";
		else if ( op == "||" ) op = "OR";

		return left + " " + op + " " + right;
	}

	// Handle constant integers
	const ConstInteger *ci = dynamic_cast<const ConstInteger*>( expr );
	if ( ci )
	{
		return to_string( ci->mValue );
	}

	// Handle constant strings
	const ConstString *cs = dynamic_cast<const ConstString*>( expr );
	if ( cs )
	{
		params.push_back( cs->mValue );
		return "?";
	}

	// Handle constant floats
	const ConstFloat *cf = dynamic_cast<const ConstFloat*>( expr );
	if ( cf )
	{
		return to_string( cf->mValue );
	}

	// Handle variable references (bind as parameters)
	const VariableExpression *ve = dynamic_cast<const VariableExpression*>( expr );
	if ( ve )
	{
		// Variable references in SQL context become parameter placeholders
		return "?";
	}

	return "?";
}

SQLStatement SQLGen::generateSelect( QueryExpression *query, Module *mod )
{
	SQLStatement result;
	string tableName = tableNameToSQL( query->mTableName );
	stringstream ss;

	ss << "SELECT * FROM " << tableName;

	string joinClause;
	string whereClause;
	string orderByClause;
	string limitClause;

	for ( auto &step : query->mSteps )
	{
		switch ( step.mType )
		{
		case QueryPipelineStep::WHERE:
			if ( !whereClause.empty() ) whereClause += " AND ";
			whereClause += exprToSQL( step.mExpression, result.params );
			break;

		case QueryPipelineStep::ORDER_BY:
			orderByClause = exprToSQL( step.mExpression, result.params );
			break;

		case QueryPipelineStep::LIMIT:
		{
			ConstInteger *ci = dynamic_cast<ConstInteger*>( (Expression*)step.mExpression );
			if ( ci )
				limitClause = to_string( ci->mValue );
			else
				limitClause = "?";
			break;
		}

		case QueryPipelineStep::JOIN:
		{
			string joinTable = tableNameToSQL( step.mJoinTable );
			string joinCond = exprToSQL( step.mExpression, result.params );
			joinClause += " JOIN " + joinTable + " ON " + joinCond;
			break;
		}

		case QueryPipelineStep::FIRST:
			limitClause = "1";
			break;

		default:
			break;
		}
	}

	if ( !joinClause.empty() ) ss << joinClause;
	if ( !whereClause.empty() ) ss << " WHERE " << whereClause;
	if ( !orderByClause.empty() ) ss << " ORDER BY " << orderByClause;
	if ( !limitClause.empty() ) ss << " LIMIT " << limitClause;

	result.sql = ss.str();
	return result;
}

SQLStatement SQLGen::generateInsert( InsertExpression *insert, Module *mod )
{
	SQLStatement result;
	string tableName = tableNameToSQL( insert->mTableName );
	stringstream ss;

	ss << "INSERT INTO " << tableName << " (";

	for ( size_t i = 0; i < insert->mFieldNames.size(); i++ )
	{
		if ( i > 0 ) ss << ", ";
		ss << insert->mFieldNames[i];
	}

	ss << ") VALUES (";

	for ( size_t i = 0; i < insert->mFieldValues.size(); i++ )
	{
		if ( i > 0 ) ss << ", ";
		ss << "?";
	}

	ss << ")";

	result.sql = ss.str();
	return result;
}

SQLStatement SQLGen::generateUpdate( UpdateExpression *update, Module *mod )
{
	SQLStatement result;
	string tableName = tableNameToSQL( update->mTableName );
	stringstream ss;

	ss << "UPDATE " << tableName;

	string setClauses;
	string whereClause;

	for ( auto &step : update->mSteps )
	{
		switch ( step.mType )
		{
		case QueryPipelineStep::WHERE:
			if ( !whereClause.empty() ) whereClause += " AND ";
			whereClause += exprToSQL( step.mExpression, result.params );
			break;

		case QueryPipelineStep::SET:
			for ( auto &field : step.mSetFields )
			{
				if ( !setClauses.empty() ) setClauses += ", ";
				setClauses += field.first + " = " +
					exprToSQL( field.second, result.params );
			}
			break;

		default:
			break;
		}
	}

	if ( !setClauses.empty() ) ss << " SET " << setClauses;
	if ( !whereClause.empty() ) ss << " WHERE " << whereClause;

	result.sql = ss.str();
	return result;
}

SQLStatement SQLGen::generateDelete( DeleteExpression *del, Module *mod )
{
	SQLStatement result;
	string tableName = tableNameToSQL( del->mTableName );
	stringstream ss;

	ss << "DELETE FROM " << tableName;

	string whereClause;

	for ( auto &step : del->mSteps )
	{
		if ( step.mType == QueryPipelineStep::WHERE )
		{
			if ( !whereClause.empty() ) whereClause += " AND ";
			whereClause += exprToSQL( step.mExpression, result.params );
		}
	}

	if ( !whereClause.empty() ) ss << " WHERE " << whereClause;

	result.sql = ss.str();
	return result;
}

string SQLGen::generateCreateTable( StructDefinition *structDef )
{
	string tableName = tableNameToSQL( structDef->getName() );
	stringstream ss;

	ss << "CREATE TABLE IF NOT EXISTS " << tableName << " (";

	const auto &fields = structDef->getFields();
	for ( size_t i = 0; i < fields.size(); i++ )
	{
		if ( i > 0 ) ss << ", ";
		ss << fields[i]->getName() << " " << blangTypeToSQL( fields[i]->getVariableType()->getName() );

		// Mark 'id' fields as PRIMARY KEY
		if ( fields[i]->getName() == "id" )
			ss << " PRIMARY KEY";
	}

	ss << ")";
	return ss.str();
}

string SQLGen::generateAddColumn( const string &tableName, const string &columnName,
	const string &columnType )
{
	return "ALTER TABLE " + tableNameToSQL( tableName ) +
		" ADD COLUMN " + columnName + " " + blangTypeToSQL( columnType );
}
