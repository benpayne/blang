#include "SchemaMigration.h"
#include "SQLGen.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

using namespace QLang;
using namespace std;

// Simple JSON serialization/deserialization for schema files.
// We use a minimal approach to avoid external dependencies.

// Parse a schema JSON file into `out`.  Returns true on success or when the
// file is absent (treated as an empty schema).
static bool parseSchemaFile( const string &path, vector<SchemaTable> &out )
{
	ifstream file( path );
	if ( !file.is_open() )
	{
		out.clear();
		return true;
	}

	// Parse a simple JSON format:
	// { "tables": [ { "name": "user", "fields": [ { "name": "id", "type": "int" }, ... ] }, ... ] }
	string content( (istreambuf_iterator<char>( file )),
		istreambuf_iterator<char>() );
	file.close();

	out.clear();

	// Minimal JSON parser for our known schema format
	size_t pos = 0;
	auto skipWs = [&]() {
		while ( pos < content.size() && isspace( content[pos] ) ) pos++;
	};
	auto expectChar = [&]( char c ) -> bool {
		skipWs();
		if ( pos < content.size() && content[pos] == c ) { pos++; return true; }
		return false;
	};
	auto parseString = [&]() -> string {
		skipWs();
		if ( pos >= content.size() || content[pos] != '"' ) return "";
		pos++; // skip opening quote
		string result;
		while ( pos < content.size() && content[pos] != '"' )
		{
			if ( content[pos] == '\\' ) { pos++; }
			if ( pos < content.size() ) result += content[pos++];
		}
		if ( pos < content.size() ) pos++; // skip closing quote
		return result;
	};

	// Parse { "tables": [ ... ] }
	if ( !expectChar( '{' ) ) return false;
	string key = parseString();
	if ( key != "tables" ) return false;
	if ( !expectChar( ':' ) ) return false;
	if ( !expectChar( '[' ) ) return false;

	skipWs();
	while ( pos < content.size() && content[pos] != ']' )
	{
		SchemaTable table;

		if ( !expectChar( '{' ) ) return false;

		// Parse table fields
		while ( pos < content.size() && content[pos] != '}' )
		{
			skipWs();
			string fieldKey = parseString();
			if ( !expectChar( ':' ) ) return false;

			if ( fieldKey == "name" )
			{
				table.name = parseString();
			}
			else if ( fieldKey == "fields" )
			{
				if ( !expectChar( '[' ) ) return false;
				skipWs();
				while ( pos < content.size() && content[pos] != ']' )
				{
					SchemaField field;
					if ( !expectChar( '{' ) ) return false;

					while ( pos < content.size() && content[pos] != '}' )
					{
						skipWs();
						string fk = parseString();
						if ( !expectChar( ':' ) ) return false;
						string fv = parseString();

						if ( fk == "name" ) field.name = fv;
						else if ( fk == "type" ) field.type = fv;

						skipWs();
						if ( pos < content.size() && content[pos] == ',' ) pos++;
					}
					expectChar( '}' );
					table.fields.push_back( field );

					skipWs();
					if ( pos < content.size() && content[pos] == ',' ) pos++;
				}
				expectChar( ']' );
			}

			skipWs();
			if ( pos < content.size() && content[pos] == ',' ) pos++;
		}
		expectChar( '}' );
		out.push_back( table );

		skipWs();
		if ( pos < content.size() && content[pos] == ',' ) pos++;
	}

	return true;
}

bool SchemaMigration::loadSchema( const string &path )
{
	return parseSchemaFile( path, mStoredSchema );
}

bool SchemaMigration::loadCurrentSchema( const string &path )
{
	return parseSchemaFile( path, mCurrentSchema );
}

bool SchemaMigration::saveSchema( const string &path )
{
	ofstream file( path );
	if ( !file.is_open() ) return false;

	file << "{\n  \"tables\": [\n";

	for ( size_t i = 0; i < mCurrentSchema.size(); i++ )
	{
		const SchemaTable &t = mCurrentSchema[i];
		if ( i > 0 ) file << ",\n";
		file << "    {\n";
		file << "      \"name\": \"" << t.name << "\",\n";
		file << "      \"fields\": [\n";

		for ( size_t j = 0; j < t.fields.size(); j++ )
		{
			const SchemaField &f = t.fields[j];
			if ( j > 0 ) file << ",\n";
			file << "        { \"name\": \"" << f.name << "\", \"type\": \"" << f.type << "\" }";
		}

		file << "\n      ]\n    }";
	}

	file << "\n  ]\n}\n";
	file.close();
	return true;
}

void SchemaMigration::extractSchema( const vector<SmartPtr<StructDefinition>> &structs )
{
	mCurrentSchema.clear();

	for ( const auto &structDef : structs )
	{
		if ( !structDef->isTable() ) continue;

		SchemaTable table;
		table.name = structDef->getName();

		const auto &fields = structDef->getFields();
		for ( const auto &field : fields )
		{
			SchemaField sf;
			sf.name = field->getName();
			sf.type = field->getVariableType()->getName();
			table.fields.push_back( sf );
		}

		mCurrentSchema.push_back( table );
	}
}

vector<MigrationStep> SchemaMigration::computeDiff()
{
	vector<MigrationStep> steps;

	// Build lookup maps
	map<string, const SchemaTable*> storedMap;
	for ( const auto &t : mStoredSchema )
		storedMap[t.name] = &t;

	map<string, const SchemaTable*> currentMap;
	for ( const auto &t : mCurrentSchema )
		currentMap[t.name] = &t;

	// Check for new tables
	for ( const auto &t : mCurrentSchema )
	{
		if ( storedMap.find( t.name ) == storedMap.end() )
		{
			// New table — generate CREATE TABLE
			MigrationStep step;
			step.type = MigrationStep::CREATE_TABLE;
			step.description = "Create table " + t.name;

			stringstream ss;
			ss << "CREATE TABLE IF NOT EXISTS ";

			// Convert name to SQL
			string sqlName;
			for ( size_t i = 0; i < t.name.size(); i++ )
			{
				char c = t.name[i];
				if ( isupper( c ) )
				{
					if ( i > 0 ) sqlName += '_';
					sqlName += (char)tolower( c );
				}
				else
					sqlName += c;
			}

			ss << sqlName << " (";
			for ( size_t i = 0; i < t.fields.size(); i++ )
			{
				if ( i > 0 ) ss << ", ";
				ss << t.fields[i].name << " " << SQLGen::blangTypeToSQL( t.fields[i].type );
				if ( t.fields[i].name == "id" )
					ss << " PRIMARY KEY";
			}
			ss << ")";

			step.sql = ss.str();
			steps.push_back( step );
		}
	}

	// Check for new columns in existing tables
	for ( const auto &t : mCurrentSchema )
	{
		auto it = storedMap.find( t.name );
		if ( it == storedMap.end() ) continue; // already handled as new table

		const SchemaTable *stored = it->second;
		map<string, string> storedFields;
		for ( const auto &f : stored->fields )
			storedFields[f.name] = f.type;

		for ( const auto &f : t.fields )
		{
			if ( storedFields.find( f.name ) == storedFields.end() )
			{
				MigrationStep step;
				step.type = MigrationStep::ADD_COLUMN;
				step.description = "Add column " + f.name + " to " + t.name;
				step.sql = SQLGen::generateAddColumn( t.name, f.name, f.type );
				steps.push_back( step );
			}
		}
	}

	// Check for removed tables
	for ( const auto &t : mStoredSchema )
	{
		if ( currentMap.find( t.name ) == currentMap.end() )
		{
			MigrationStep step;
			step.type = MigrationStep::DROP_TABLE;
			step.description = "Drop table " + t.name + " (DESTRUCTIVE — requires @drop)";
			step.isDestructive = true;

			string sqlName;
			for ( size_t i = 0; i < t.name.size(); i++ )
			{
				char c = t.name[i];
				if ( isupper( c ) )
				{
					if ( i > 0 ) sqlName += '_';
					sqlName += (char)tolower( c );
				}
				else
					sqlName += c;
			}
			step.sql = "DROP TABLE " + sqlName;
			steps.push_back( step );
		}
	}

	// Check for removed columns in existing tables
	for ( const auto &t : mStoredSchema )
	{
		auto it = currentMap.find( t.name );
		if ( it == currentMap.end() ) continue; // already handled as dropped table

		const SchemaTable *current = it->second;
		map<string, string> currentFields;
		for ( const auto &f : current->fields )
			currentFields[f.name] = f.type;

		for ( const auto &f : t.fields )
		{
			if ( currentFields.find( f.name ) == currentFields.end() )
			{
				MigrationStep step;
				step.type = MigrationStep::DROP_COLUMN;
				step.description = "Drop column " + f.name + " from " + t.name + " (DESTRUCTIVE — requires @drop)";
				step.isDestructive = true;
				// SQLite doesn't support DROP COLUMN before 3.35.0
				// For now, generate the SQL and let the runtime handle it
				string sqlName;
				for ( size_t i = 0; i < t.name.size(); i++ )
				{
					char c = t.name[i];
					if ( isupper( c ) )
					{
						if ( i > 0 ) sqlName += '_';
						sqlName += (char)tolower( c );
					}
					else
						sqlName += c;
				}
				step.sql = "ALTER TABLE " + sqlName + " DROP COLUMN " + f.name;
				steps.push_back( step );
			}
		}
	}

	return steps;
}

string SchemaMigration::generateSQL()
{
	vector<MigrationStep> steps = computeDiff();
	stringstream ss;

	for ( const auto &step : steps )
	{
		if ( step.isDestructive )
			ss << "-- DESTRUCTIVE: " << step.description << "\n";
		else
			ss << "-- " << step.description << "\n";
		ss << step.sql << ";\n\n";
	}

	return ss.str();
}

string SchemaMigration::preview()
{
	vector<MigrationStep> steps = computeDiff();
	if ( steps.empty() )
		return "No schema changes detected.\n";

	stringstream ss;
	ss << "Migration preview:\n";

	for ( const auto &step : steps )
	{
		if ( step.isDestructive )
			ss << "  [DESTRUCTIVE] " << step.description << "\n";
		else
			ss << "  " << step.description << "\n";
		ss << "    SQL: " << step.sql << "\n";
	}

	return ss.str();
}

bool SchemaMigration::hasDestructiveChanges()
{
	vector<MigrationStep> steps = computeDiff();
	for ( const auto &step : steps )
	{
		if ( step.isDestructive ) return true;
	}
	return false;
}
