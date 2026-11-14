// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DiscIO/DolphinRVZ.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>

#include "DiscIO/Blob.h"
#include "DiscIO/DiscUtils.h"
#include "DiscIO/Enums.h"
#include "DiscIO/Volume.h"
#include "DiscIO/WIABlob.h"

namespace
{
constexpr int RvzChunkSize = 128 * 1024;
constexpr int ZstdCompressionLevel = 5;
// WIA only supports chunk sizes that are a multiple of 2 MiB.
constexpr int WiaChunkSize = 2 * 1024 * 1024;
constexpr int WiaCompressionLevel = 5;
constexpr int GczBlockSize = 128 * 1024;
std::mutex conversion_mutex;
std::unordered_set<std::string> active_output_paths;

void SetError(char* error_message, unsigned int error_capacity, const char* message)
{
  if (error_message && error_capacity > 0)
  {
	std::strncpy(error_message, message, error_capacity - 1);
	error_message[error_capacity - 1] = '\0';
  }
}
}

int DolphinDiscConvert(const char* input_path, const char* output_path, int output_format,
					   DolphinRVZProgressCallback progress_callback, void* context,
					   char* error_message, unsigned int error_capacity, unsigned int* notes)
{
  if (notes)
	*notes = 0;

  if (!input_path || !output_path || !*input_path || !*output_path)
  {
	SetError(error_message, error_capacity, "Input and output paths are required.");
	return 1;
  }

  if (output_format < DOLPHIN_DISC_FORMAT_ISO || output_format > DOLPHIN_DISC_FORMAT_GCZ)
  {
	SetError(error_message, error_capacity, "Unsupported output format.");
	return 10;
  }

  try
  {
	std::lock_guard conversion_lock(conversion_mutex);
	if (!active_output_paths.insert(output_path).second)
	{
	  SetError(error_message, error_capacity, "Another Dolphin conversion is already running.");
	  return 9;
	}
	struct OutputPathGuard
	{
	  const std::string& path;
	  ~OutputPathGuard() { active_output_paths.erase(path); }
	} output_path_guard{output_path};

	std::unique_ptr<DiscIO::BlobReader> reader = DiscIO::CreateBlobReader(input_path);
	if (!reader)
	{
	  SetError(error_message, error_capacity, "The input file could not be opened.");
	  return 2;
	}

	std::unique_ptr<DiscIO::VolumeDisc> volume = DiscIO::CreateDisc(input_path);
	if (output_format != DOLPHIN_DISC_FORMAT_ISO)
	{
	  if (!volume)
	  {
		SetError(error_message, error_capacity, "The input is not a valid GameCube/Wii disc image.");
		return 3;
	  }

	  // Compressed formats need to know the exact size of the source to be able to restore it.
	  if (reader->GetDataSizeType() != DiscIO::DataSizeType::Accurate)
	  {
		SetError(error_message, error_capacity,
				 "The input does not store its exact size (for example WBFS or CISO). "
				 "Convert it to a plain ISO first.");
		return 4;
	  }

	  if (notes)
	  {
		if (volume->IsNKit())
		  *notes |= DOLPHIN_DISC_NOTE_NKIT;
		if (output_format == DOLPHIN_DISC_FORMAT_GCZ &&
			volume->GetVolumeType() == DiscIO::Platform::WiiDisc && !volume->IsDatelDisc())
		{
		  *notes |= DOLPHIN_DISC_NOTE_WII_GCZ;
		}
	  }
	}

	const DiscIO::CompressCB callback = [progress_callback, context](const std::string&, float percent) {
	  if (progress_callback)
		return progress_callback(std::clamp(percent * 100.0f, 0.0f, 100.0f), context) != 0;
	  return true;
	};

	bool success = false;
	switch (output_format)
	{
	case DOLPHIN_DISC_FORMAT_ISO:
	  success = DiscIO::ConvertToPlain(reader.get(), input_path, output_path, callback);
	  break;
	case DOLPHIN_DISC_FORMAT_RVZ:
	  success = DiscIO::ConvertToWIAOrRVZ(reader.get(), input_path, output_path, true,
										  DiscIO::WIARVZCompressionType::Zstd,
										  ZstdCompressionLevel, RvzChunkSize, callback);
	  break;
	case DOLPHIN_DISC_FORMAT_WIA:
	  success = DiscIO::ConvertToWIAOrRVZ(reader.get(), input_path, output_path, false,
										  DiscIO::WIARVZCompressionType::LZMA2,
										  WiaCompressionLevel, WiaChunkSize, callback);
	  break;
	case DOLPHIN_DISC_FORMAT_GCZ:
	  success = DiscIO::ConvertToGCZ(
		  reader.get(), input_path, output_path,
		  volume->GetVolumeType() == DiscIO::Platform::WiiDisc ? 1u : 0u, GczBlockSize, callback);
	  break;
	}

	if (!success)
	{
	  SetError(error_message, error_capacity, "Dolphin's disc conversion failed.");
	  return 6;
	}

	if (error_message && error_capacity > 0)
	  error_message[0] = '\0';
	return 0;
  }
  catch (const std::exception& exception)
  {
	SetError(error_message, error_capacity, exception.what());
	return 7;
  }
  catch (...)
  {
	SetError(error_message, error_capacity, "An unknown error occurred during conversion.");
	return 8;
  }
}

int DolphinRVZConvert(const char* input_path, const char* output_path, int to_rvz,
					  DolphinRVZProgressCallback progress_callback, void* context,
					  char* error_message, unsigned int error_capacity)
{
  return DolphinDiscConvert(input_path, output_path,
							to_rvz ? DOLPHIN_DISC_FORMAT_RVZ : DOLPHIN_DISC_FORMAT_ISO,
							progress_callback, context, error_message, error_capacity, nullptr);
}
