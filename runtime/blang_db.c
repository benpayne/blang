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

/* ---- Global / Named Connection Registry ---- */

/*
 * A small process-wide registry so generated code does not have to thread a
 * connection pointer through every query.  The default connection is used when
 * a table struct has no @db("name") annotation; named connections back the
 * @db("name") routing.  Kept driver-independent (it only calls __blang_db_open,
 * which is defined for both the real and stub backends below).
 */

#define BLANG_DB_MAX_NAMED 16

static BlangDBConn *g_default_conn = NULL;

static struct
{
	char *name;
	BlangDBConn *conn;
} g_named_conns[BLANG_DB_MAX_NAMED];
static int g_num_named = 0;

BlangDBDriver __blang_db_driver_from_name( const char *name )
{
	if ( name && ( strcmp( name, "postgres" ) == 0 ||
		strcmp( name, "postgresql" ) == 0 || strcmp( name, "pg" ) == 0 ) )
		return BLANG_DB_POSTGRES;
	return BLANG_DB_SQLITE;
}

void __blang_db_set_default( BlangDBConn *conn )
{
	g_default_conn = conn;
}

BlangDBConn *__blang_db_default( void )
{
	if ( g_default_conn )
		return g_default_conn;

	/* Lazy fallback: open from environment so single-file programs and tests
	   work without any [database] config plumbing. */
	const char *url = getenv( "BLANG_DATABASE_URL" );
	if ( url && *url )
	{
		const char *drv = getenv( "BLANG_DATABASE_DRIVER" );
		const char *err = NULL;
		BlangDBConn *conn = __blang_db_open(
			__blang_db_driver_from_name( drv ), url, &err );
		if ( conn )
			g_default_conn = conn;
		return conn;
	}

	return NULL;
}

void __blang_db_register( const char *name, BlangDBConn *conn )
{
	if ( !name )
		return;

	/* Replace if the name already exists. */
	for ( int i = 0; i < g_num_named; i++ )
	{
		if ( strcmp( g_named_conns[i].name, name ) == 0 )
		{
			g_named_conns[i].conn = conn;
			return;
		}
	}

	if ( g_num_named < BLANG_DB_MAX_NAMED )
	{
		g_named_conns[g_num_named].name = strdup( name );
		g_named_conns[g_num_named].conn = conn;
		g_num_named++;
	}
}

BlangDBConn *__blang_db_get( const char *name )
{
	if ( name )
	{
		for ( int i = 0; i < g_num_named; i++ )
		{
			if ( strcmp( g_named_conns[i].name, name ) == 0 )
				return g_named_conns[i].conn;
		}
	}
	return __blang_db_default();
}

int __blang_db_exec_raw_default( const char *sql )
{
	const char *err = NULL;
	return __blang_db_exec_raw( __blang_db_default(), sql, &err );
}

/* Resolve an "env:VAR" connection string against the environment at runtime,
   so a url configured in blang.toml ([database] url = "env:DATABASE_URL")
   picks up the deployment value rather than a build-time constant.  Returns
   the resolved string, or NULL if the env var is unset (sets *error_msg). */
static const char *resolve_conn_string( const char *connection_string,
	const char **error_msg )
{
	if ( connection_string && strncmp( connection_string, "env:", 4 ) == 0 )
	{
		const char *resolved = getenv( connection_string + 4 );
		if ( !resolved || !*resolved )
		{
			if ( error_msg )
				*error_msg = "environment variable for database url is not set";
			return NULL;
		}
		return resolved;
	}
	return connection_string;
}

/* ---- SQLite Backend ---- */

#ifdef BLANG_HAS_SQLITE
#include <sqlite3.h>

static BlangDBConn *sqlite_open( const char *connection_string, const char **error_msg )
{
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

static BlangDBResult *sqlite_query( BlangDBConn *conn, const char *sql,
	const char **params, int num_params, const char **error_msg )
{
	sqlite3 *db = (sqlite3 *)conn->driver_data;
	sqlite3_stmt *stmt = NULL;

	int rc = sqlite3_prepare_v2( db, sql, -1, &stmt, NULL );
	if ( rc != SQLITE_OK )
	{
		if ( error_msg ) *error_msg = sqlite3_errmsg( db );
		return NULL;
	}

	for ( int i = 0; i < num_params; i++ )
	{
		if ( params[i] )
			sqlite3_bind_text( stmt, i + 1, params[i], -1, SQLITE_TRANSIENT );
		else
			sqlite3_bind_null( stmt, i + 1 );
	}

	int num_cols = sqlite3_column_count( stmt );
	BlangDBResult *result = result_alloc( num_cols );

	for ( int i = 0; i < num_cols; i++ )
		result->column_names[i] = strdup( sqlite3_column_name( stmt, i ) );

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

static int sqlite_exec( BlangDBConn *conn, const char *sql,
	const char **params, int num_params, const char **error_msg )
{
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

static int sqlite_exec_raw( BlangDBConn *conn, const char *sql, const char **error_msg )
{
	sqlite3 *db = (sqlite3 *)conn->driver_data;
	char *err = NULL;
	int rc = sqlite3_exec( db, sql, NULL, NULL, &err );

	if ( rc != SQLITE_OK )
	{
		/* sqlite3_exec allocates `err`; copy it into a connection-owned buffer
		   and free the sqlite allocation so we neither leak nor hand back a
		   pointer freed on the next call. */
		if ( error_msg )
		{
			static char errbuf[512];
			snprintf( errbuf, sizeof( errbuf ), "%s",
				err ? err : sqlite3_errmsg( db ) );
			*error_msg = errbuf;
		}
		if ( err )
			sqlite3_free( err );
		return -1;
	}

	return 0;
}

static void sqlite_close( BlangDBConn *conn )
{
	if ( conn->driver_data )
		sqlite3_close( (sqlite3 *)conn->driver_data );
}

#endif /* BLANG_HAS_SQLITE */

/* ---- Postgres Backend ---- */

/*
 * Compiled only when BLANG_HAS_POSTGRES is defined (libpq available).
 * Postgres uses $1,$2,... placeholders rather than `?`; codegen stays
 * driver-neutral by always emitting `?`, and the rewrite happens here.
 */

#ifdef BLANG_HAS_POSTGRES
#include <libpq-fe.h>

/* Rewrite `?` placeholders to Postgres `$1,$2,...`.  Caller frees the result.
   Naive scan (parameters are always bound, so SQL contains no literal `?`). */
static char *pg_rewrite_placeholders( const char *sql )
{
	size_t len = strlen( sql );
	/* Worst case each `?` becomes `$NN` — allocate generously. */
	char *out = (char *)malloc( len * 4 + 1 );
	size_t o = 0;
	int n = 0;
	for ( size_t i = 0; i < len; i++ )
	{
		if ( sql[i] == '?' )
			o += sprintf( out + o, "$%d", ++n );
		else
			out[o++] = sql[i];
	}
	out[o] = '\0';
	return out;
}

static BlangDBConn *pg_open( const char *connection_string, const char **error_msg )
{
	PGconn *pg = PQconnectdb( connection_string );
	if ( PQstatus( pg ) != CONNECTION_OK )
	{
		if ( error_msg )
		{
			static char errbuf[512];
			snprintf( errbuf, sizeof( errbuf ), "%s", PQerrorMessage( pg ) );
			*error_msg = errbuf;
		}
		PQfinish( pg );
		return NULL;
	}

	BlangDBConn *conn = (BlangDBConn *)calloc( 1, sizeof( BlangDBConn ) );
	conn->driver = BLANG_DB_POSTGRES;
	conn->driver_data = pg;
	return conn;
}

static BlangDBResult *pg_query( BlangDBConn *conn, const char *sql,
	const char **params, int num_params, const char **error_msg )
{
	PGconn *pg = (PGconn *)conn->driver_data;
	char *rewritten = pg_rewrite_placeholders( sql );
	PGresult *res = PQexecParams( pg, rewritten, num_params, NULL,
		params, NULL, NULL, 0 /* text results */ );
	free( rewritten );

	if ( PQresultStatus( res ) != PGRES_TUPLES_OK )
	{
		if ( error_msg )
		{
			static char errbuf[512];
			snprintf( errbuf, sizeof( errbuf ), "%s", PQerrorMessage( pg ) );
			*error_msg = errbuf;
		}
		PQclear( res );
		return NULL;
	}

	int num_cols = PQnfields( res );
	int num_rows = PQntuples( res );
	BlangDBResult *result = result_alloc( num_cols );

	for ( int c = 0; c < num_cols; c++ )
		result->column_names[c] = strdup( PQfname( res, c ) );

	for ( int r = 0; r < num_rows; r++ )
	{
		char **row = (char **)calloc( num_cols, sizeof( char * ) );
		for ( int c = 0; c < num_cols; c++ )
		{
			const char *val = PQgetisnull( res, r, c )
				? "" : PQgetvalue( res, r, c );
			row[c] = strdup( val );
		}
		result_add_row( result, row );
	}

	PQclear( res );
	return result;
}

static int pg_exec( BlangDBConn *conn, const char *sql,
	const char **params, int num_params, const char **error_msg )
{
	PGconn *pg = (PGconn *)conn->driver_data;
	char *rewritten = pg_rewrite_placeholders( sql );
	PGresult *res = PQexecParams( pg, rewritten, num_params, NULL,
		params, NULL, NULL, 0 );
	free( rewritten );

	ExecStatusType st = PQresultStatus( res );
	if ( st != PGRES_COMMAND_OK && st != PGRES_TUPLES_OK )
	{
		if ( error_msg )
		{
			static char errbuf[512];
			snprintf( errbuf, sizeof( errbuf ), "%s", PQerrorMessage( pg ) );
			*error_msg = errbuf;
		}
		PQclear( res );
		return -1;
	}

	const char *affected = PQcmdTuples( res );
	int n = ( affected && *affected ) ? atoi( affected ) : 0;
	PQclear( res );
	return n;
}

static int pg_exec_raw( BlangDBConn *conn, const char *sql, const char **error_msg )
{
	PGconn *pg = (PGconn *)conn->driver_data;
	PGresult *res = PQexec( pg, sql );
	ExecStatusType st = PQresultStatus( res );
	if ( st != PGRES_COMMAND_OK && st != PGRES_TUPLES_OK )
	{
		if ( error_msg )
		{
			static char errbuf[512];
			snprintf( errbuf, sizeof( errbuf ), "%s", PQerrorMessage( pg ) );
			*error_msg = errbuf;
		}
		PQclear( res );
		return -1;
	}
	PQclear( res );
	return 0;
}

static void pg_close( BlangDBConn *conn )
{
	if ( conn->driver_data )
		PQfinish( (PGconn *)conn->driver_data );
}

#endif /* BLANG_HAS_POSTGRES */

/* ---- Public Driver Dispatch ---- */

BlangDBConn *__blang_db_open( BlangDBDriver driver, const char *connection_string,
	const char **error_msg )
{
	connection_string = resolve_conn_string( connection_string, error_msg );
	if ( connection_string == NULL )
		return NULL;

	if ( driver == BLANG_DB_SQLITE )
	{
#ifdef BLANG_HAS_SQLITE
		return sqlite_open( connection_string, error_msg );
#else
		if ( error_msg )
			*error_msg = "SQLite support not compiled (install libsqlite3-dev)";
		return NULL;
#endif
	}
	else if ( driver == BLANG_DB_POSTGRES )
	{
#ifdef BLANG_HAS_POSTGRES
		return pg_open( connection_string, error_msg );
#else
		if ( error_msg )
			*error_msg = "Postgres support not compiled (install libpq-dev)";
		return NULL;
#endif
	}

	if ( error_msg ) *error_msg = "Unknown database driver";
	return NULL;
}

void __blang_db_close( BlangDBConn *conn )
{
	if ( !conn ) return;
#ifdef BLANG_HAS_SQLITE
	if ( conn->driver == BLANG_DB_SQLITE ) sqlite_close( conn );
#endif
#ifdef BLANG_HAS_POSTGRES
	if ( conn->driver == BLANG_DB_POSTGRES ) pg_close( conn );
#endif
	free( conn );
}

BlangDBResult *__blang_db_query( BlangDBConn *conn, const char *sql,
	const char **params, int num_params, const char **error_msg )
{
	if ( !conn )
	{
		if ( error_msg ) *error_msg = "Invalid connection";
		return NULL;
	}
#ifdef BLANG_HAS_SQLITE
	if ( conn->driver == BLANG_DB_SQLITE )
		return sqlite_query( conn, sql, params, num_params, error_msg );
#endif
#ifdef BLANG_HAS_POSTGRES
	if ( conn->driver == BLANG_DB_POSTGRES )
		return pg_query( conn, sql, params, num_params, error_msg );
#endif
	if ( error_msg ) *error_msg = "Database driver not compiled";
	return NULL;
}

int __blang_db_exec( BlangDBConn *conn, const char *sql,
	const char **params, int num_params, const char **error_msg )
{
	if ( !conn )
	{
		if ( error_msg ) *error_msg = "Invalid connection";
		return -1;
	}
#ifdef BLANG_HAS_SQLITE
	if ( conn->driver == BLANG_DB_SQLITE )
		return sqlite_exec( conn, sql, params, num_params, error_msg );
#endif
#ifdef BLANG_HAS_POSTGRES
	if ( conn->driver == BLANG_DB_POSTGRES )
		return pg_exec( conn, sql, params, num_params, error_msg );
#endif
	if ( error_msg ) *error_msg = "Database driver not compiled";
	return -1;
}

int __blang_db_exec_raw( BlangDBConn *conn, const char *sql, const char **error_msg )
{
	if ( !conn )
	{
		if ( error_msg ) *error_msg = "Invalid connection";
		return -1;
	}
#ifdef BLANG_HAS_SQLITE
	if ( conn->driver == BLANG_DB_SQLITE )
		return sqlite_exec_raw( conn, sql, error_msg );
#endif
#ifdef BLANG_HAS_POSTGRES
	if ( conn->driver == BLANG_DB_POSTGRES )
		return pg_exec_raw( conn, sql, error_msg );
#endif
	if ( error_msg ) *error_msg = "Database driver not compiled";
	return -1;
}
