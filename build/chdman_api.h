// Copyright (C) 2026 chrisis123 and emucompress contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef CHDMAN_API_H
#define CHDMAN_API_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#define CHDMAN_API __declspec(dllimport)
#else
#define CHDMAN_API
#endif

#include <stddef.h>

/* Executes CHDMAN and writes captured diagnostics into the supplied buffer. */
CHDMAN_API int chdman_execute(
	int argc,
	const char *const *argv,
	char *error_buffer,
	size_t error_capacity);

#ifdef __cplusplus
}
#endif

#endif
