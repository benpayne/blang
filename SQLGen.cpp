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

string SQLGen::exprToSQL( const Expression *expr, vector<const Expression*> &paramExprs )
{
	// Handle query field references: .field -> field (column name)
	const QueryFieldExpression *field = dynamic_cast<const QueryFieldExpression*>( expr );
	if ( field )
		return field->getFieldName();

	// Handle binary operations: a == b -> a = b, a != b -> a <> b
	const OperationsExpression *ops = dynamic_cast<const OperationsExpression*>( expr );
	if ( ops )
	{
		string left = exprToSQL( ops->mOp1, paramExprs );
		string right = exprToSQL( ops->mOp2, paramExprs );
		string op = ops->mOperation;

		// Map BLang operators to SQL operators
		if ( op == "==" ) op = "=";
		else if ( op == "!=" ) op = "<>";
		else if ( op == "&&" ) op = "AND";
		else if ( op == "||" ) op = "OR";

		return left + " " + op + " " + right;
	}

	// Numeric constants are inlined directly — safe (no injection risk) and
	// avoids a runtime bind for a value already known at compile time.
	const ConstInteger *ci = dynamic_cast<const ConstInteger*>( expr );
	if ( ci )
		return to_string( ci->mValue );

	const ConstFloat *cf = dynamic_cast<const ConstFloat*>( expr );
	if ( cf )
		return to_string( cf->mValue );

	// Every other value expression (string literals, variables, field
	// accesses, calls, ...) becomes a bound `?` parameter so its runtime value
	// is supplied safely.  The expression is recorded for codegen in
	// placeholder order.
	paramExprs.push_back( expr );
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

	// Collect params per-clause so the final paramExprs order matches the
	// emitted SQL order (JOIN, WHERE, ORDER BY, LIMIT) rather than the order
	// the pipeline steps happened to appear in.
	vector<const Expression*> joinParams;
	vector<const Expression*> whereParams;
	vector<const Expression*> orderParams;
	vector<const Expression*> limitParams;

	for ( auto &step : query->mSteps )
	{
		switch ( step.mType )
		{
		case QueryPipelineStep::WHERE:
			if ( !whereClause.empty() ) whereClause += " AND ";
			whereClause += exprToSQL( step.mExpression, whereParams );
			break;

		case QueryPipelineStep::ORDER_BY:
			orderByClause = exprToSQL( step.mExpression, orderParams );
			break;

		case QueryPipelineStep::LIMIT:
		{
			ConstInteger *ci = dynamic_cast<ConstInteger*>( (Expression*)step.mExpression );
			if ( ci )
			{
				limitClause = to_string( ci->mValue );
			}
			else
			{
				limitClause = "?";
				limitParams.push_back( (const Expression*)step.mExpression );
			}
			break;
		}

		case QueryPipelineStep::JOIN:
		{
			string joinTable = tableNameToSQL( step.mJoinTable );
			string joinCond = exprToSQL( step.mExpression, joinParams );
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

	result.paramExprs.insert( result.paramExprs.end(), joinParams.begin(), joinParams.end() );
	result.paramExprs.insert( result.paramExprs.end(), whereParams.begin(), whereParams.end() );
	result.paramExprs.insert( result.paramExprs.end(), orderParams.begin(), orderParams.end() );
	result.paramExprs.insert( result.paramExprs.end(), limitParams.begin(), limitParams.end() );

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
		result.paramExprs.push_back( (const Expression*)insert->mFieldValues[i] );
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

	// Collect SET and WHERE params separately: in the emitted SQL, SET precedes
	// WHERE, so their `?` placeholders (and thus paramExprs) must be ordered
	// SET-first regardless of pipeline step order.
	string setClauses;
	string whereClause;
	vector<const Expression*> setParams;
	vector<const Expression*> whereParams;

	for ( auto &step : update->mSteps )
	{
		switch ( step.mType )
		{
		case QueryPipelineStep::WHERE:
			if ( !whereClause.empty() ) whereClause += " AND ";
			whereClause += exprToSQL( step.mExpression, whereParams );
			break;

		case QueryPipelineStep::SET:
			for ( auto &field : step.mSetFields )
			{
				if ( !setClauses.empty() ) setClauses += ", ";
				setClauses += field.first + " = " +
					exprToSQL( field.second, setParams );
			}
			break;

		default:
			break;
		}
	}

	if ( !setClauses.empty() ) ss << " SET " << setClauses;
	if ( !whereClause.empty() ) ss << " WHERE " << whereClause;

	result.paramExprs.insert( result.paramExprs.end(), setParams.begin(), setParams.end() );
	result.paramExprs.insert( result.paramExprs.end(), whereParams.begin(), whereParams.end() );

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
			whereClause += exprToSQL( step.mExpression, result.paramExprs );
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
