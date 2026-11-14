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

// Output formats for DolphinDiscConvert.
enum
{
  DOLPHIN_DISC_FORMAT_ISO = 0,
  DOLPHIN_DISC_FORMAT_RVZ = 1,
  DOLPHIN_DISC_FORMAT_WIA = 2,
  DOLPHIN_DISC_FORMAT_GCZ = 3,
};

// Bits reported through DolphinDiscConvert's notes output.
enum
{
  // The source is an NKit image, so the output is still NKit and cannot restore the original disc.
  DOLPHIN_DISC_NOTE_NKIT = 1,
  // A Wii disc was written to GCZ, which saves very little space on encrypted Wii data.
  DOLPHIN_DISC_NOTE_WII_GCZ = 2,
};

// Converts a GameCube/Wii disc image to ISO, RVZ, WIA or GCZ. The input may be any container
// Dolphin can read. notes may be null. Returns 0 on success.
DOLPHIN_RVZ_EXPORT int DolphinDiscConvert(const char* input_path, const char* output_path,
										 int output_format,
										 DolphinRVZProgressCallback progress_callback,
										 void* context, char* error_message,
										 unsigned int error_capacity, unsigned int* notes);

// Legacy entry point: to_rvz != 0 writes RVZ, otherwise ISO.
DOLPHIN_RVZ_EXPORT int DolphinRVZConvert(const char* input_path, const char* output_path,
										int to_rvz, DolphinRVZProgressCallback progress_callback,
										void* context, char* error_message, unsigned int error_capacity);

#ifdef __cplusplus
}
#endif
