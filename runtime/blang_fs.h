#ifndef BLANG_FS_H
#define BLANG_FS_H

#include "blang_string.h"
#include "blang_buffer.h"
#include "blang_array.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
   File handle operations (thin wrappers around POSIX syscalls)
   ======================================================================== */

/* Open a file. mode: "r", "w", "a", "rw". Returns fd on success, -1 on error. */
int __blang_file_open( const char *path, const char *mode );

/* Close a file descriptor. */
void __blang_file_close( int fd );

/* Write a BlangString to fd. Returns bytes written, -1 on error. */
int __blang_file_write_string( int fd, const BlangString *data );

/* Read from fd into a buffer. Returns bytes read (0 = EOF, -1 = error). */
int64_t __blang_file_read_into_buffer( int fd, BlangBuffer *buf, int64_t max_len );

/* Read from fd into a byte array. Returns bytes read (0 = EOF, -1 = error). */
int64_t __blang_file_read_into_byte_array( int fd, BlangArray *arr, int64_t max_len );

/* Seek to offset. whence: 0=SET, 1=CUR, 2=END. Returns new position, -1 on error. */
int64_t __blang_file_seek( int fd, int64_t offset, int whence );

/* Flush (fsync) a file descriptor. Returns 0 on success, -1 on error. */
int __blang_file_flush( int fd );

/* ========================================================================
   Filesystem operations
   ======================================================================== */

/* Stat a path and return the file type.
   Returns: -1 = not found, 0 = regular file, 1 = directory. */
int __blang_fs_file_type( const char *path );

/* Stat a path and return the file size in bytes.
   Returns -1 if the path doesn't exist. */
int64_t __blang_fs_file_size( const char *path );

/* Remove a file or empty directory. Returns 0 on success, -1 on error. */
int __blang_fs_remove( const char *path );

/* Create a directory with mode 0755. Returns 0 on success, -1 on error. */
int __blang_fs_mkdir( const char *path );

/* List directory entries (excludes "." and "..").
   Returns a BlangArray of BlangString* with elem_dtor set. */
BlangArray *__blang_fs_list_dir( const char *path );

#ifdef __cplusplus
}
#endif

#endif /* BLANG_FS_H */
