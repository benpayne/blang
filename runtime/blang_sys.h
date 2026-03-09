#ifndef BLANG_SYS_H
#define BLANG_SYS_H

#include "blang_array.h"
#include "blang_string.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize sys module: capture argc/argv into a BlangArray of BlangString.
   Must be called once before main body executes. */
void __blang_sys_init( int argc, char **argv );

/* Return the args array. Returns the same array each call (retained). */
BlangArray *__blang_sys_get_args( void );

/* Exit the process with the given status code. */
void __blang_sys_exit( int code );

#ifdef __cplusplus
}
#endif

#endif /* BLANG_SYS_H */
