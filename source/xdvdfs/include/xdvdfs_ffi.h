#ifndef XDVDFS_FFI_H
#define XDVDFS_FFI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Return codes from xdvdfs_pack. */
enum xdvdfs_status {
	XDVDFS_SUCCESS = 0,
	XDVDFS_INVALID_ARGUMENT = 1,
	XDVDFS_OPERATION_FAILED = 2,
	XDVDFS_PANIC = 3,
	XDVDFS_CANCELLED = 4
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

/*
 * Callbacks for xdvdfs_walk. Each returns zero to continue; any other value
 * stops the walk and xdvdfs_walk returns XDVDFS_CANCELLED. Paths are relative,
 * UTF-8 and use '/' separators. Empty files get no data callbacks.
 */
typedef struct xdvdfs_walk_callbacks {
	int (*on_start)(uint64_t total_bytes, uint64_t file_count, void* ctx);
	int (*on_directory)(const char* path, void* ctx);
	int (*on_file_begin)(const char* path, uint64_t size, void* ctx);
	int (*on_file_data)(const unsigned char* data, size_t length, void* ctx);
} xdvdfs_walk_callbacks;

/*
 * Streams the contents of an XDVDFS image (XISO, XGD1, XGD2 or XGD3 layout) to
 * the callbacks without writing to disk. The error buffer follows the same
 * rules as xdvdfs_pack.
 */
int xdvdfs_walk(
	const char* image_path,
	const xdvdfs_walk_callbacks* callbacks,
	void* ctx,
	unsigned char* error_buffer,
	size_t error_capacity);

#ifdef __cplusplus
}
#endif

#endif
