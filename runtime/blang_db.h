#ifndef BLANG_DB_H_
#define BLANG_DB_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Database Connection ---- */

typedef struct BlangDBConn BlangDBConn;
typedef struct BlangDBResult BlangDBResult;
typedef struct BlangDBRow BlangDBRow;

/* Supported database drivers */
typedef enum {
	BLANG_DB_SQLITE,
	BLANG_DB_POSTGRES
} BlangDBDriver;

/* Open a database connection.
   driver: which driver to use
   connection_string: driver-specific connection info
     - SQLite: file path (or ":memory:" for in-memory)
     - Postgres: libpq connection string
   Returns NULL on failure, sets *error_msg. */
BlangDBConn *__blang_db_open( BlangDBDriver driver, const char *connection_string,
	const char **error_msg );

/* Close a database connection and free resources. */
void __blang_db_close( BlangDBConn *conn );

/* ---- Global / Named Connection Registry ---- */

/* Set the process-wide default connection (used by query/insert/update/delete
   codegen when a table struct carries no @db("name") annotation). */
void __blang_db_set_default( BlangDBConn *conn );

/* Return the process-wide default connection.  If none has been set, this
   lazily opens one from the BLANG_DATABASE_URL environment variable
   (driver from BLANG_DATABASE_DRIVER, defaulting to "sqlite"), caches it as
   the default, and returns it.  Returns NULL if no default is available. */
BlangDBConn *__blang_db_default( void );

/* Register a connection under a name for @db("name") routing. */
void __blang_db_register( const char *name, BlangDBConn *conn );

/* Look up a named connection.  Falls back to the default connection if the
   name is not registered. */
BlangDBConn *__blang_db_get( const char *name );

/* Map a driver name ("sqlite" / "postgres") to a BlangDBDriver.  Defaults to
   SQLite for unrecognized names. */
BlangDBDriver __blang_db_driver_from_name( const char *name );

/* ---- Query Execution ---- */

/* Execute a parameterized SQL query.
   sql: SQL string with ? placeholders
   params: array of string parameters (NULL-terminated)
   Returns result set or NULL on error. */
BlangDBResult *__blang_db_query( BlangDBConn *conn, const char *sql,
	const char **params, int num_params, const char **error_msg );

/* Execute a parameterized SQL statement (INSERT/UPDATE/DELETE).
   Returns number of affected rows, or -1 on error. */
int __blang_db_exec( BlangDBConn *conn, const char *sql,
	const char **params, int num_params, const char **error_msg );

/* ---- Result Set ---- */

/* Get number of rows in result. */
int __blang_db_result_count( BlangDBResult *result );

/* Get number of columns in result. */
int __blang_db_result_columns( BlangDBResult *result );

/* Get column name by index. */
const char *__blang_db_result_column_name( BlangDBResult *result, int col );

/* Get a string value from the result. Row and col are 0-indexed.
   Returns NULL if out of bounds. */
const char *__blang_db_result_get( BlangDBResult *result, int row, int col );

/* Get an integer value from the result. */
int64_t __blang_db_result_get_int( BlangDBResult *result, int row, int col );

/* Get a float value from the result. */
double __blang_db_result_get_float( BlangDBResult *result, int row, int col );

/* Free a result set. */
void __blang_db_result_free( BlangDBResult *result );

/* ---- Schema Operations (for migrations) ---- */

/* Execute raw SQL (e.g., CREATE TABLE, ALTER TABLE).
   Returns 0 on success, -1 on error. */
int __blang_db_exec_raw( BlangDBConn *conn, const char *sql, const char **error_msg );

/* Execute raw SQL against the default connection (convenience for generated
   code and migrations).  Returns 0 on success, -1 on error. */
int __blang_db_exec_raw_default( const char *sql );

#ifdef __cplusplus
}
#endif

#endif /* BLANG_DB_H_ */
