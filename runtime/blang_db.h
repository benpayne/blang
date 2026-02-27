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

#ifdef __cplusplus
}
#endif

#endif /* BLANG_DB_H_ */
