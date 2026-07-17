#ifndef BLANG_SCHEMA_MIGRATION_H_
#define BLANG_SCHEMA_MIGRATION_H_

#include <string>
#include <vector>
#include <map>

#include "Type.h"

namespace QLang
{

// Represents a single field in the stored schema
struct SchemaField
{
	std::string name;
	std::string type;
};

// Represents a table in the stored schema
struct SchemaTable
{
	std::string name;
	std::vector<SchemaField> fields;
};

// A migration step to apply
struct MigrationStep
{
	enum StepType {
		CREATE_TABLE,
		ADD_COLUMN,
		DROP_COLUMN,    // destructive — requires @drop annotation
		DROP_TABLE      // destructive — requires @drop annotation
	};

	StepType type;
	std::string sql;
	std::string description;
	bool isDestructive = false;
};

// Schema migration engine.
// Compares current table struct definitions against a stored schema snapshot
// and generates migration SQL.
class SchemaMigration
{
public:
	// Load stored schema from a JSON file (into the stored snapshot).
	// Returns true on success (or if file doesn't exist — treated as empty schema).
	bool loadSchema( const std::string &path );

	// Load the current schema from a JSON file (into the current snapshot).
	// Used by `bcc migrate`, which obtains the current schema by running
	// `qcc --emit-schema` rather than parsing in-process.
	bool loadCurrentSchema( const std::string &path );

	// Save current schema to a JSON file.
	bool saveSchema( const std::string &path );

	// Extract schema from parsed table struct definitions
	void extractSchema( const std::vector<SmartPtr<StructDefinition>> &structs );

	// Compute the diff between stored and current schema.
	// Returns list of migration steps needed.
	std::vector<MigrationStep> computeDiff();

	// Generate SQL for all steps
	std::string generateSQL();

	// Preview: show what would change (human-readable)
	std::string preview();

	// Check if any destructive changes need confirmation
	bool hasDestructiveChanges();

private:
	std::vector<SchemaTable> mStoredSchema;  // from .blang/schema.json
	std::vector<SchemaTable> mCurrentSchema; // from parsed table structs
};

} // namespace QLang

#endif // BLANG_SCHEMA_MIGRATION_H_
