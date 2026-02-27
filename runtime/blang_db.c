#include "blang_db.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- Internal Structures ---- */

/* Result set: stored as a 2D array of strings */
struct BlangDBResult
{
	char **column_names;
	char ***rows;    /* rows[row][col] */
	int num_rows;
	int num_cols;
	int row_capacity;
};

/* Connection handle — driver-specific data behind an opaque pointer */
struct BlangDBConn
{
	BlangDBDriver driver;
	void *driver_data;   /* sqlite3* or PGconn* */
};

/* ---- Result Set Helpers ---- */

static BlangDBResult *result_alloc( int num_cols )
{
	BlangDBResult *r = (BlangDBResult *)calloc( 1, sizeof( BlangDBResult ) );
	r->num_cols = num_cols;
	r->row_capacity = 16;
	r->rows = (char ***)calloc( r->row_capacity, sizeof( char ** ) );
	r->column_names = (char **)calloc( num_cols, sizeof( char * ) );
	return r;
}

static void result_add_row( BlangDBResult *r, char **row )
{
	if ( r->num_rows >= r->row_capacity )
	{
		r->row_capacity *= 2;
		r->rows = (char ***)realloc( r->rows, r->row_capacity * sizeof( char ** ) );
	}
	r->rows[r->num_rows++] = row;
}

int __blang_db_result_count( BlangDBResult *result )
{
	return result ? result->num_rows : 0;
}

int __blang_db_result_columns( BlangDBResult *result )
{
	return result ? result->num_cols : 0;
}

const char *__blang_db_result_column_name( BlangDBResult *result, int col )
{
	if ( !result || col < 0 || col >= result->num_cols ) return NULL;
	return result->column_names[col];
}

const char *__blang_db_result_get( BlangDBResult *result, int row, int col )
{
	if ( !result || row < 0 || row >= result->num_rows ||
		 col < 0 || col >= result->num_cols )
		return NULL;
	return result->rows[row][col];
}

int64_t __blang_db_result_get_int( BlangDBResult *result, int row, int col )
{
	const char *val = __blang_db_result_get( result, row, col );
	return val ? strtoll( val, NULL, 10 ) : 0;
}

double __blang_db_result_get_float( BlangDBResult *result, int row, int col )
{
	const char *val = __blang_db_result_get( result, row, col );
	return val ? strtod( val, NULL ) : 0.0;
}

void __blang_db_result_free( BlangDBResult *result )
{
	if ( !result ) return;

	for ( int i = 0; i < result->num_rows; i++ )
	{
		for ( int j = 0; j < result->num_cols; j++ )
			free( result->rows[i][j] );
		free( result->rows[i] );
	}
	free( result->rows );

	for ( int j = 0; j < result->num_cols; j++ )
		free( result->column_names[j] );
	free( result->column_names );

	free( result );
}

/* ---- SQLite Backend ---- */

/*
 * SQLite support is compiled conditionally.
 * When BLANG_HAS_SQLITE is defined (i.e., libsqlite3 is available),
 * the real implementation is used.  Otherwise, stub functions return errors.
 */

#ifdef BLANG_HAS_SQLITE
#include <sqlite3.h>

BlangDBConn *__blang_db_open( BlangDBDriver driver, const char *connection_string,
	const char **error_msg )
{
	if ( driver != BLANG_DB_SQLITE )
	{
		if ( error_msg ) *error_msg = "Only SQLite driver is currently supported";
		return NULL;
	}

	sqlite3 *db = NULL;
	int rc = sqlite3_open( connection_string, &db );
	if ( rc != SQLITE_OK )
	{
		if ( error_msg ) *error_msg = sqlite3_errmsg( db );
		sqlite3_close( db );
		return NULL;
	}

	BlangDBConn *conn = (BlangDBConn *)calloc( 1, sizeof( BlangDBConn ) );
	conn->driver = BLANG_DB_SQLITE;
	conn->driver_data = db;
	return conn;
}

void __blang_db_close( BlangDBConn *conn )
{
	if ( !conn ) return;
	if ( conn->driver == BLANG_DB_SQLITE && conn->driver_data )
		sqlite3_close( (sqlite3 *)conn->driver_data );
	free( conn );
}

BlangDBResult *__blang_db_query( BlangDBConn *conn, const char *sql,
	const char **params, int num_params, const char **error_msg )
{
	if ( !conn || conn->driver != BLANG_DB_SQLITE )
	{
		if ( error_msg ) *error_msg = "Invalid connection";
		return NULL;
	}

	sqlite3 *db = (sqlite3 *)conn->driver_data;
	sqlite3_stmt *stmt = NULL;

	int rc = sqlite3_prepare_v2( db, sql, -1, &stmt, NULL );
	if ( rc != SQLITE_OK )
	{
		if ( error_msg ) *error_msg = sqlite3_errmsg( db );
		return NULL;
	}

	/* Bind parameters */
	for ( int i = 0; i < num_params; i++ )
	{
		if ( params[i] )
			sqlite3_bind_text( stmt, i + 1, params[i], -1, SQLITE_TRANSIENT );
		else
			sqlite3_bind_null( stmt, i + 1 );
	}

	int num_cols = sqlite3_column_count( stmt );
	BlangDBResult *result = result_alloc( num_cols );

	/* Column names */
	for ( int i = 0; i < num_cols; i++ )
		result->column_names[i] = strdup( sqlite3_column_name( stmt, i ) );

	/* Fetch rows */
	while ( ( rc = sqlite3_step( stmt ) ) == SQLITE_ROW )
	{
		char **row = (char **)calloc( num_cols, sizeof( char * ) );
		for ( int i = 0; i < num_cols; i++ )
		{
			const char *val = (const char *)sqlite3_column_text( stmt, i );
			row[i] = val ? strdup( val ) : strdup( "" );
		}
		result_add_row( result, row );
	}

	sqlite3_finalize( stmt );

	if ( rc != SQLITE_DONE )
	{
		if ( error_msg ) *error_msg = sqlite3_errmsg( db );
		__blang_db_result_free( result );
		return NULL;
	}

	return result;
}

int __blang_db_exec( BlangDBConn *conn, const char *sql,
	const char **params, int num_params, const char **error_msg )
{
	if ( !conn || conn->driver != BLANG_DB_SQLITE )
	{
		if ( error_msg ) *error_msg = "Invalid connection";
		return -1;
	}

	sqlite3 *db = (sqlite3 *)conn->driver_data;
	sqlite3_stmt *stmt = NULL;

	int rc = sqlite3_prepare_v2( db, sql, -1, &stmt, NULL );
	if ( rc != SQLITE_OK )
	{
		if ( error_msg ) *error_msg = sqlite3_errmsg( db );
		return -1;
	}

	for ( int i = 0; i < num_params; i++ )
	{
		if ( params[i] )
			sqlite3_bind_text( stmt, i + 1, params[i], -1, SQLITE_TRANSIENT );
		else
			sqlite3_bind_null( stmt, i + 1 );
	}

	rc = sqlite3_step( stmt );
	sqlite3_finalize( stmt );

	if ( rc != SQLITE_DONE )
	{
		if ( error_msg ) *error_msg = sqlite3_errmsg( db );
		return -1;
	}

	return sqlite3_changes( db );
}

int __blang_db_exec_raw( BlangDBConn *conn, const char *sql, const char **error_msg )
{
	if ( !conn || conn->driver != BLANG_DB_SQLITE )
	{
		if ( error_msg ) *error_msg = "Invalid connection";
		return -1;
	}

	sqlite3 *db = (sqlite3 *)conn->driver_data;
	char *err = NULL;
	int rc = sqlite3_exec( db, sql, NULL, NULL, &err );

	if ( rc != SQLITE_OK )
	{
		if ( error_msg ) *error_msg = err ? err : sqlite3_errmsg( db );
		/* Note: err is allocated by sqlite3 and will be freed on next call */
		return -1;
	}

	return 0;
}

#else /* !BLANG_HAS_SQLITE */

/* Stub implementations when SQLite is not available */

BlangDBConn *__blang_db_open( BlangDBDriver driver, const char *connection_string,
	const char **error_msg )
{
	(void)driver;
	(void)connection_string;
	if ( error_msg ) *error_msg = "Database support not compiled (install libsqlite3-dev)";
	return NULL;
}

void __blang_db_close( BlangDBConn *conn ) { (void)conn; }

BlangDBResult *__blang_db_query( BlangDBConn *conn, const char *sql,
	const char **params, int num_params, const char **error_msg )
{
	(void)conn; (void)sql; (void)params; (void)num_params;
	if ( error_msg ) *error_msg = "Database support not compiled";
	return NULL;
}

int __blang_db_exec( BlangDBConn *conn, const char *sql,
	const char **params, int num_params, const char **error_msg )
{
	(void)conn; (void)sql; (void)params; (void)num_params;
	if ( error_msg ) *error_msg = "Database support not compiled";
	return -1;
}

int __blang_db_exec_raw( BlangDBConn *conn, const char *sql, const char **error_msg )
{
	(void)conn; (void)sql;
	if ( error_msg ) *error_msg = "Database support not compiled";
	return -1;
}

#endif /* BLANG_HAS_SQLITE */
