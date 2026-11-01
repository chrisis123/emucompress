#ifndef XDVDFS_FFI_H
#define XDVDFS_FFI_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Return codes from xdvdfs_pack. */
enum xdvdfs_status {
	XDVDFS_SUCCESS = 0,
	XDVDFS_INVALID_ARGUMENT = 1,
	XDVDFS_OPERATION_FAILED = 2,
	XDVDFS_PANIC = 3
};

/*
 * Packs a directory or raw ISO image into an XDVDFS image.
 *
 * source_path and image_path are null-terminated UTF-8 strings. The error
 * buffer is optional; when supplied, it receives a null-terminated UTF-8
 * diagnostic truncated to error_capacity bytes including the terminator.
 */
int xdvdfs_pack(
	const char* source_path,
	const char* image_path,
	unsigned char* error_buffer,
	size_t error_capacity);

/*
 * Extracts an XDVDFS image into a directory.
 *
 * image_path and destination_path are null-terminated UTF-8 strings. The
 * error buffer follows the same rules as xdvdfs_pack.
 */
int xdvdfs_unpack(
	const char* image_path,
	const char* destination_path,
	unsigned char* error_buffer,
	size_t error_capacity);

#ifdef __cplusplus
}
#endif

#endif
