#ifndef _LOGGING_H_
#define _LOGGING_H_

#include <cstdio>
#include <cstdint>

#define LOG_CAT_ALL		0xffffffff
#define LOG_CAT_DEFAULT	(1u << 31)

#define LOG_LVL_ERR		0
#define LOG_LVL_WARN	2
#define LOG_LVL_NOTICE	4
#define LOG_LVL_INFO	5
#define LOG_LVL_NOISE	6

#define SET_LOG_CAT(x)		static uint32_t _file_log_cat __attribute__ ((unused)) = x
#define SET_LOG_LEVEL(x)	static int _file_log_level __attribute__ ((unused)) = x

#ifdef JH_VERBOSE_LOGGING

#define LOG( fmt, ... )		fprintf( stderr, fmt "\n", ##__VA_ARGS__ )
#define LOG_ERR( fmt, ... )	fprintf( stderr, "ERROR: " fmt "\n", ##__VA_ARGS__ )

class Tracer {
public:
	Tracer( int level, const char *func, const char *file, int line,
			volatile uint32_t&, volatile int& file_level ) :
		mLevel( level ), mFunc( func ), mFileLevel( file_level )
	{
		if ( mLevel <= file_level )
			fprintf( stderr, "[TRACE] %s begin\n", mFunc );
	}
	~Tracer()
	{
		if ( mLevel <= mFileLevel )
			fprintf( stderr, "[TRACE] %s end\n", mFunc );
	}
	int getLevel() { return mLevel; }
private:
	int mLevel;
	const char *mFunc;
	volatile int& mFileLevel;
};

#define TRACE_BEGIN(level) \
	Tracer _tracer( level, __PRETTY_FUNCTION__, __FILE__, __LINE__, _file_log_cat, _file_log_level )

#else // !JH_VERBOSE_LOGGING

#define LOG( fmt, ... )
#define LOG_ERR( fmt, ... )	fprintf( stderr, "ERROR: " fmt "\n", ##__VA_ARGS__ )

#define TRACE_BEGIN(x)

#endif // JH_VERBOSE_LOGGING

#define TRACE_END()

#endif // _LOGGING_H_
