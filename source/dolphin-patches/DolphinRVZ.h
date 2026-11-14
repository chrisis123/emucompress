// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#ifdef _WIN32
#define DOLPHIN_RVZ_EXPORT __declspec(dllexport)
#else
#define DOLPHIN_RVZ_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C"
{
#endif

typedef int (*DolphinRVZProgressCallback)(float percent, void* context);

DOLPHIN_RVZ_EXPORT int DolphinRVZConvert(const char* input_path, const char* output_path,
                                        int to_rvz, DolphinRVZProgressCallback progress_callback,
                                        void* context, char* error_message, unsigned int error_capacity);

#ifdef __cplusplus
}
#endif
