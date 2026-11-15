// emucompress - disc image conversion front end
// Copyright (C) 2026 chrisis123 and emucompress contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation; either version 2 of the License, or (at your option) any later
// version. It is distributed WITHOUT ANY WARRANTY. See LICENSE and
// THIRD_PARTY_NOTICES.md for the license terms and third-party components.

#define NOMINMAX
#include <windows.h>
#include <algorithm>
namespace Gdiplus { using std::min; using std::max; }
#include <gdiplus.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "xdvdfs_ffi.h"
#include "chdman_api.h"
#include "zarchive/zarchivereader.h"
#include "zarchive/zarchivewriter.h"

// The ZArchive header defines static helpers that this file does not use.
#pragma warning(disable : 4505)

namespace
{
constexpr wchar_t WindowClassName[] = L"XdvdfsDragDropWindow";
constexpr UINT ConversionTimerId = 1;
constexpr UINT ConversionCompleteMessage = WM_APP + 1;
constexpr UINT DolphinProgressMessage = WM_APP + 2;
constexpr UINT_PTR StatusControlId = 1001;
constexpr UINT_PTR ProgressControlId = 1002;
constexpr UINT_PTR PackButtonId = 1003;
constexpr UINT_PTR UnpackButtonId = 1004;
constexpr UINT_PTR CreateCdButtonId = 1006;
constexpr UINT_PTR CreateDvdButtonId = 1007;
constexpr UINT_PTR ExtractCdButtonId = 1008;
constexpr UINT_PTR ExtractDvdButtonId = 1009;
constexpr UINT_PTR ExtractCdToIsoButtonId = 1010;
constexpr UINT_PTR XisoToIsoButtonId = 1011;
constexpr UINT_PTR PickXdvdfsFolderButtonId = 1012;
constexpr UINT_PTR PickChdmanFolderButtonId = 1013;
constexpr UINT_PTR IsoToRvzButtonId = 1014;
constexpr UINT_PTR RvzToIsoButtonId = 1015;
constexpr UINT_PTR PickDolphinFolderButtonId = 1016;
constexpr UINT_PTR AboutMenuItemId = 1017;
constexpr UINT_PTR BrowseFilesButtonId = 1018;
constexpr UINT_PTR ReadmeMenuItemId = 1019;
constexpr UINT_PTR CancelOperationButtonId = 1020;
constexpr UINT_PTR ResetXdvdfsFolderButtonId = 1021;
constexpr UINT_PTR ResetChdmanFolderButtonId = 1022;
constexpr UINT_PTR ResetDolphinFolderButtonId = 1023;
constexpr UINT_PTR IsoToZarButtonId = 1024;
constexpr UINT_PTR ZarToXisoButtonId = 1025;
constexpr UINT_PTR IsoToWiaButtonId = 1026;
constexpr UINT_PTR WiaToIsoButtonId = 1027;
constexpr UINT_PTR IsoToGczButtonId = 1028;
constexpr UINT_PTR GczToIsoButtonId = 1029;
// Values understood by DolphinDiscConvert in DolphinRVZ.dll.
constexpr int DolphinFormatIso = 0;
constexpr int DolphinFormatRvz = 1;
constexpr int DolphinFormatWia = 2;
constexpr int DolphinFormatGcz = 3;
constexpr unsigned int DolphinNoteNkit = 1;
constexpr unsigned int DolphinNoteWiiGcz = 2;
constexpr int ConversionKindIsoToZar = 6;
constexpr int ConversionKindZarToXiso = 7;
constexpr wchar_t ProgressClassName[] = L"EmucompressProgress";
constexpr int ApplicationIconResourceId = 101;
constexpr unsigned long long DualLayerDvdImageSize = 8547991552ULL;

HWND main_window = nullptr;
HWND status_control = nullptr;
HWND progress_control = nullptr;
HWND pack_button = nullptr;
HWND unpack_button = nullptr;
HWND create_cd_button = nullptr;
HWND create_dvd_button = nullptr;
HWND extract_cd_button = nullptr;
HWND extract_dvd_button = nullptr;
HWND extract_cd_to_iso_button = nullptr;
HWND iso_to_rvz_button = nullptr;
HWND rvz_to_iso_button = nullptr;
HWND cancel_operation_button = nullptr;
HWND pick_dolphin_folder_button = nullptr;
std::thread conversion_thread;
std::atomic<bool> conversion_active = false;
std::atomic<bool> cancel_requested = false;
std::atomic<bool> conversion_unpack = false;
std::atomic<int> conversion_kind = 0;
std::atomic<unsigned long long> total_bytes = 1;
std::atomic<unsigned long long> completed_bytes = 0;
std::atomic<int> dolphin_progress_percent = 0;
std::atomic<int> dolphin_target_format = DolphinFormatIso;
std::atomic<unsigned int> dolphin_notes = 0;
std::atomic<unsigned long long> dolphin_progress_bytes = 0;
std::atomic<unsigned long long> zar_progress_bytes = 0;
std::atomic<int> zar_phase = 1;
std::chrono::steady_clock::time_point conversion_start;
std::mutex result_mutex;
int conversion_result = XDVDFS_OPERATION_FAILED;
std::string conversion_error;
std::wstring conversion_output;
std::wstring conversion_operation;
std::wstring conversion_notice;
std::wstring input_path;
std::vector<std::wstring> selected_input_paths;
std::vector<std::wstring> batch_input_paths;
std::vector<std::wstring> batch_errors;
size_t batch_input_index = 0;
size_t batch_success_count = 0;
UINT_PTR batch_operation_id = 0;
bool batch_running = false;
bool batch_stop_requested = false;
bool cancel_current_operation = false;
std::wstring xdvdfs_output_directory;
std::wstring chdman_output_directory;
std::wstring dolphin_output_directory;

enum class StatusTone { Neutral, Success, Error };
StatusTone status_tone = StatusTone::Neutral;
std::wstring last_status_text;
unsigned long long selection_bytes = 0;
bool ui_busy = false;

void SetStatus(const std::wstring& text);
void SetOperationsEnabled(bool busy);
void RefreshSelection();
void UpdateFolderUi();

std::string ToUtf8(const std::wstring& value)
{
    if (value.empty())
    {
        return {};
    }

    const int length = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (length <= 0)
    {
        return {};
    }

    std::string result(static_cast<size_t>(length), '\0');
    if (WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, value.c_str(), -1, &result[0], length, nullptr, nullptr) <= 0)
    {
        return {};
    }

    result.resize(static_cast<size_t>(length - 1));
    return result;
}

std::wstring PickOutputDirectory(const std::wstring& current_directory)
{
    const HRESULT initialization = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitialize = SUCCEEDED(initialization);
    if (FAILED(initialization) && initialization != RPC_E_CHANGED_MODE)
    {
        SetStatus(L"Unable to initialize the output-folder picker.");
        return {};
    }

    IFileDialog* dialog = nullptr;
    HRESULT result = CoCreateInstance(
        CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(result))
    {
        if (uninitialize) CoUninitialize();
        SetStatus(L"Unable to open the output-folder picker.");
        return {};
    }

    FILEOPENDIALOGOPTIONS options{};
    result = dialog->GetOptions(&options);
    if (SUCCEEDED(result))
    {
        result = dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    }
    if (SUCCEEDED(result) && !current_directory.empty())
    {
        IShellItem* current = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(current_directory.c_str(), nullptr, IID_PPV_ARGS(&current))))
        {
            dialog->SetFolder(current);
            current->Release();
        }
    }

    if (SUCCEEDED(result)) result = dialog->Show(main_window);

    std::wstring selected_directory;
    if (SUCCEEDED(result))
    {
        IShellItem* item = nullptr;
        result = dialog->GetResult(&item);
        if (SUCCEEDED(result))
        {
            PWSTR path = nullptr;
            result = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
            if (SUCCEEDED(result))
            {
                selected_directory = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }

    dialog->Release();
    if (uninitialize) CoUninitialize();
    if (FAILED(result) && result != HRESULT_FROM_WIN32(ERROR_CANCELLED))
    {
        SetStatus(L"Unable to read the selected output folder.");
    }
    return selected_directory;
}

std::vector<std::wstring> PickInputFiles()
{
    const HRESULT initialization = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitialize = SUCCEEDED(initialization);
    if (FAILED(initialization) && initialization != RPC_E_CHANGED_MODE)
    {
        SetStatus(L"Unable to initialize the file picker.");
        return {};
    }

    IFileOpenDialog* dialog = nullptr;
    HRESULT result = CoCreateInstance(
        CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(result))
    {
        if (uninitialize) CoUninitialize();
        SetStatus(L"Unable to open the file picker.");
        return {};
    }

    FILEOPENDIALOGOPTIONS options{};
    result = dialog->GetOptions(&options);
    if (SUCCEEDED(result))
    {
        result = dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_ALLOWMULTISELECT);
    }
    if (SUCCEEDED(result)) result = dialog->Show(main_window);

    std::vector<std::wstring> selected_files;
    if (SUCCEEDED(result))
    {
        IShellItemArray* items = nullptr;
        result = dialog->GetResults(&items);
        if (SUCCEEDED(result))
        {
            DWORD count = 0;
            result = items->GetCount(&count);
            for (DWORD i = 0; SUCCEEDED(result) && i < count; ++i)
            {
                IShellItem* item = nullptr;
                result = items->GetItemAt(i, &item);
                if (SUCCEEDED(result))
                {
                    PWSTR path = nullptr;
                    result = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
                    if (SUCCEEDED(result))
                    {
                        selected_files.emplace_back(path);
                        CoTaskMemFree(path);
                    }
                    item->Release();
                }
            }
            items->Release();
        }
    }

    dialog->Release();
    if (uninitialize) CoUninitialize();
    if (FAILED(result) && result != HRESULT_FROM_WIN32(ERROR_CANCELLED))
    {
        SetStatus(L"Unable to read the selected files.");
        return {};
    }
    return selected_files;
}

std::wstring OutputPathInDirectory(const std::wstring& output, const std::wstring& directory)
{
    if (directory.empty()) return output;
    const size_t separator = output.find_last_of(L"\\/");
    const std::wstring filename = separator == std::wstring::npos ? output : output.substr(separator + 1);
    return directory + L"\\" + filename;
}

std::wstring ToWide(const char* value)
{
    if (value == nullptr || *value == '\0')
    {
        return {};
    }

    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, nullptr, 0);
    if (length <= 0)
    {
        return L"Unknown error";
    }

    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, &result[0], length);
    result.resize(static_cast<size_t>(length - 1));
    return result;
}

bool EndsWith(const std::wstring& value, const wchar_t* suffix)
{
    const size_t suffix_length = wcslen(suffix);
    return value.size() >= suffix_length &&
        _wcsicmp(value.c_str() + value.size() - suffix_length, suffix) == 0;
}

std::wstring OutputPathFor(const std::wstring& input, bool unpack)
{
    return unpack ? input.substr(0, input.size() - 9) : input + L".xiso.iso";
}

std::wstring ReplaceExtension(const std::wstring& input, const wchar_t* extension)
{
    const size_t separator = input.find_last_of(L"\\/");
    const size_t dot = input.find_last_of(L'.');
    const size_t stem_end = dot != std::wstring::npos &&
        (separator == std::wstring::npos || dot > separator)
        ? dot
        : input.size();
    return input.substr(0, stem_end) + L"." + extension;
}

std::wstring StandardIsoPathForXiso(const std::wstring& input)
{
    return input.substr(0, input.size() - 9) + L".iso";
}

std::wstring ChdmanDllPath()
{
    wchar_t module_path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, module_path, ARRAYSIZE(module_path));
    if (length == 0 || length >= ARRAYSIZE(module_path))
    {
        return L"chdman.dll";
    }

    std::wstring path(module_path, length);
    const size_t separator = path.find_last_of(L"\\/");
    if (separator != std::wstring::npos)
    {
        path.resize(separator + 1);
        path += L"chdman.dll";
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            return path;
        }
    }

    return L"chdman.dll";
}

std::wstring DolphinRvzDllPath()
{
    wchar_t module_path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, module_path, ARRAYSIZE(module_path));
    if (length > 0 && length < ARRAYSIZE(module_path))
    {
        std::wstring path(module_path, length);
        const size_t separator = path.find_last_of(L"\\/");
        if (separator != std::wstring::npos)
        {
            path.resize(separator + 1);
            path += L"DolphinRVZ.dll";
            if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                return path;
            }
        }
    }
    return L"DolphinRVZ.dll";
}

unsigned long long FileSize(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
    {
        return 0;
    }

    ULARGE_INTEGER size{};
    size.HighPart = data.nFileSizeHigh;
    size.LowPart = data.nFileSizeLow;
    return size.QuadPart;
}

bool ExtendImageToSize(const std::wstring& path, unsigned long long minimum_size, std::string& error)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        error = "Unable to open repacked ISO for final sizing (Windows error " +
            std::to_string(GetLastError()) + ")";
        return false;
    }

    LARGE_INTEGER current_size{};
    bool succeeded = GetFileSizeEx(file, &current_size) != FALSE;
    DWORD error_code = succeeded ? ERROR_SUCCESS : GetLastError();
    const unsigned long long final_size = succeeded
        ? std::max<unsigned long long>(minimum_size, static_cast<unsigned long long>(current_size.QuadPart))
        : 0;
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(final_size);
    if (succeeded && !SetFilePointerEx(file, position, nullptr, FILE_BEGIN))
    {
        succeeded = false;
        error_code = GetLastError();
    }
    if (succeeded && !SetEndOfFile(file))
    {
        succeeded = false;
        error_code = GetLastError();
    }
    CloseHandle(file);

    if (!succeeded)
    {
        error = "Unable to extend repacked ISO (Windows error " + std::to_string(error_code) + ")";
        DeleteFileW(path.c_str());
        return false;
    }
    return true;
}

unsigned long long DirectorySize(const std::wstring& path)
{
    WIN32_FIND_DATAW entry{};
    const std::wstring pattern = path + L"\\*";
    HANDLE search = FindFirstFileW(pattern.c_str(), &entry);
    if (search == INVALID_HANDLE_VALUE)
    {
        return FileSize(path);
    }

    unsigned long long total = 0;
    do
    {
        if (wcscmp(entry.cFileName, L".") == 0 || wcscmp(entry.cFileName, L"..") == 0)
        {
            continue;
        }

        const std::wstring child = path + L"\\" + entry.cFileName;
        if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            total += DirectorySize(child);
        }
        else
        {
            ULARGE_INTEGER size{};
            size.HighPart = entry.nFileSizeHigh;
            size.LowPart = entry.nFileSizeLow;
            total += size.QuadPart;
        }
    } while (FindNextFileW(search, &entry));

    FindClose(search);
    return total;
}

unsigned long long CurrentOutputSize(const std::wstring& path, bool unpack)
{
    return unpack ? DirectorySize(path) : FileSize(path);
}

StatusTone ToneForStatus(const std::wstring& text)
{
    auto starts_with = [&text](const wchar_t* prefix)
    {
        return text.compare(0, wcslen(prefix), prefix) == 0;
    };
    if (starts_with(L"Completed") || starts_with(L"Batch complete"))
        return StatusTone::Success;
    if (starts_with(L"Conversion failed") || starts_with(L"Unable") ||
        starts_with(L"Output already exists") || text.find(L" requires ") != std::wstring::npos)
        return StatusTone::Error;
    return StatusTone::Neutral;
}

void SetStatus(const std::wstring& text)
{
    if (text == last_status_text)
        return;
    last_status_text = text;
    status_tone = ToneForStatus(text);
    SetWindowTextW(status_control, text.c_str());
}

void SetCancelButtonEnabled(bool enabled)
{
    if (cancel_operation_button != nullptr)
        EnableWindow(cancel_operation_button, enabled ? TRUE : FALSE);
    if (enabled)
        SetOperationsEnabled(true);
}

std::wstring FormatEta(unsigned long long completed, unsigned long long total)
{
    if (completed == 0 || total == 0)
    {
        return L"ETA: calculating...";
    }

    const double elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - conversion_start).count();
    if (elapsed <= 0.0)
    {
        return L"ETA: calculating...";
    }

    const double bytes_per_second = static_cast<double>(completed) / elapsed;
    const auto remaining_seconds = static_cast<unsigned long long>(
        (static_cast<double>(total - (completed < total ? completed : total)) / bytes_per_second) + 0.5);
    const auto minutes = remaining_seconds / 60;
    const auto seconds = remaining_seconds % 60;
    return L"ETA: " + std::to_wstring(minutes) + L":" +
        (seconds < 10 ? L"0" : L"") + std::to_wstring(seconds);
}

void UpdateProgress(bool unpack)
{
    if (conversion_kind.load() == 5)
    {
        const unsigned long long completed = dolphin_progress_bytes.load();
        const unsigned long long total = std::max<unsigned long long>(1, total_bytes.load());
        const int percentage = dolphin_progress_percent.load();
        SetStatus(conversion_operation + L" " + std::to_wstring(percentage) + L"%  " +
            FormatEta(completed, total));
        return;
    }

    if (conversion_kind.load() == ConversionKindIsoToZar || conversion_kind.load() == ConversionKindZarToXiso)
    {
        // ZAR to XISO has two phases of equal weight: extracting the archive, then building the XISO.
        const bool two_phase = conversion_kind.load() == ConversionKindZarToXiso;
        const unsigned long long content = std::max<unsigned long long>(1, total_bytes.load());
        unsigned long long completed = zar_progress_bytes.load();
        unsigned long long total = content;
        std::wstring label = conversion_operation;
        if (two_phase)
        {
            total = content * 2;
            if (zar_phase.load() == 2)
            {
                completed = content + (std::min)(FileSize(conversion_output), content);
                label = L"Building XISO from the extracted files...";
            }
            else
            {
                label = L"Extracting ZAR archive...";
            }
        }
        const unsigned long long raw_percentage = (std::min)(completed, total) * 100 / total;
        const int percentage = static_cast<int>(raw_percentage > 99 ? 99 : raw_percentage);
        SendMessageW(progress_control, PBM_SETPOS, percentage, 0);
        SetStatus(label + L" " + std::to_wstring(percentage) + L"%  " + FormatEta(completed, total));
        return;
    }

    const unsigned long long total = total_bytes.load();
    const unsigned long long completed = CurrentOutputSize(conversion_output, unpack);
    completed_bytes.store(completed);

    const unsigned long long raw_percentage = completed * 100 / total;
    const int percentage = static_cast<int>(raw_percentage > 99 ? 99 : raw_percentage);
    SendMessageW(progress_control, PBM_SETPOS, percentage, 0);
    const std::wstring operation = conversion_kind.load() == 3
        ? conversion_operation + L" "
        : (unpack ? L"Extracting XISO... " : L"Creating XISO... ");
    SetStatus(operation +
        std::to_wstring(percentage) + L"%  " + FormatEta(completed, total));
}

void StartConversion(const std::wstring& input, bool to_standard_iso = false)
{
    if (conversion_active.exchange(true))
    {
        SetStatus(L"A conversion is already running.");
        return;
    }
    cancel_requested.store(false);
    cancel_current_operation = false;

    const bool unpack = EndsWith(input, L".xiso.iso");
    if (conversion_thread.joinable())
    {
        conversion_thread.join();
    }

    const std::wstring default_output = to_standard_iso
        ? StandardIsoPathForXiso(input)
        : OutputPathFor(input, unpack);
    const std::wstring output = OutputPathInDirectory(default_output, xdvdfs_output_directory);
    const auto total = FileSize(input);
    const unsigned long long minimum_output_size = (std::max)(DualLayerDvdImageSize, total);
    total_bytes.store(to_standard_iso ? minimum_output_size : (total == 0 ? 1 : total));
    conversion_unpack.store(false);
    conversion_kind.store(to_standard_iso ? 4 : (unpack ? 2 : 1));
    completed_bytes.store(0);
    dolphin_progress_bytes.store(0);
    dolphin_progress_percent.store(0);
    conversion_output = output;
    conversion_start = std::chrono::steady_clock::now();
    SendMessageW(progress_control, PBM_SETPOS, 0, 0);
    SetStatus(to_standard_iso
        ? L"Converting XISO to ISO... ETA: calculating..."
        : (unpack ? L"Extracting XISO... ETA: calculating..." : L"Creating XISO... ETA: calculating..."));
    SetTimer(main_window, ConversionTimerId, 250, nullptr);
    SetCancelButtonEnabled(true);

    conversion_thread = std::thread([input, output, unpack, to_standard_iso, minimum_output_size]()
    {
        int status = XDVDFS_OPERATION_FAILED;
        std::string error_message;
        try
        {
            std::vector<unsigned char> error(4096, 0);
            const std::string input_utf8 = ToUtf8(input);
            const std::string output_utf8 = ToUtf8(output);
            status = to_standard_iso
                ? xdvdfs_pack(input_utf8.c_str(), output_utf8.c_str(), error.data(), error.size())
                : (unpack
                    ? xdvdfs_unpack(input_utf8.c_str(), output_utf8.c_str(), error.data(), error.size())
                    : xdvdfs_pack(input_utf8.c_str(), output_utf8.c_str(), error.data(), error.size()));

            if (status == XDVDFS_SUCCESS && to_standard_iso &&
                !ExtendImageToSize(output, minimum_output_size, error_message))
            {
                status = XDVDFS_OPERATION_FAILED;
            }

            if (error_message.empty())
            {
                error_message.assign(reinterpret_cast<const char*>(error.data()));
            }
        }
        catch (const std::exception& exception)
        {
            error_message = std::string("XDVDFS conversion failed: ") + exception.what();
        }
        catch (...)
        {
            error_message = "XDVDFS conversion failed with an unexpected error";
        }
        {
            std::lock_guard<std::mutex> lock(result_mutex);
            conversion_result = status;
            conversion_error = error_message;
        }
        conversion_active.store(false);
        PostMessageW(main_window, ConversionCompleteMessage, 0, 0);
    });
}

void StartDolphinConversion(const std::wstring& input, int target_format)
{
    if (conversion_active.exchange(true))
    {
        SetStatus(L"A conversion is already running.");
        return;
    }
    cancel_requested.store(false);
    cancel_current_operation = false;
    if (conversion_thread.joinable())
    {
        conversion_thread.join();
    }

    const wchar_t* output_extension = L"iso";
    const wchar_t* operation_text = L"Converting GameCube/Wii disc image to ISO...";
    const wchar_t* status_text = L"Converting disc image to ISO...";
    switch (target_format)
    {
    case DolphinFormatRvz:
        output_extension = L"rvz";
        operation_text = L"Compressing GameCube/Wii ISO to RVZ...";
        status_text = operation_text;
        break;
    case DolphinFormatWia:
        output_extension = L"wia";
        operation_text = L"Compressing GameCube/Wii ISO to WIA...";
        status_text = operation_text;
        break;
    case DolphinFormatGcz:
        output_extension = L"gcz";
        operation_text = L"Compressing GameCube/Wii ISO to GCZ...";
        status_text = operation_text;
        break;
    default:
        target_format = DolphinFormatIso;
        break;
    }

    const std::wstring output = OutputPathInDirectory(
        ReplaceExtension(input, output_extension), dolphin_output_directory);
    const bool output_existed = GetFileAttributesW(output.c_str()) != INVALID_FILE_ATTRIBUTES;
    if (output_existed)
    {
        SetStatus(L"Output already exists; choose a different output folder or rename the existing file.");
        conversion_active.store(false);
        return;
    }
    conversion_output = output;
    total_bytes.store(std::max<unsigned long long>(1, FileSize(input)));
    completed_bytes.store(0);
    dolphin_progress_bytes.store(0);
    dolphin_progress_percent.store(0);
    conversion_unpack.store(false);
    conversion_kind.store(5);
    dolphin_target_format.store(target_format);
    dolphin_notes.store(0);
    conversion_operation = operation_text;
    conversion_start = std::chrono::steady_clock::now();
    SendMessageW(progress_control, PBM_SETPOS, 0, 0);
    SendMessageW(progress_control, PBM_SETMARQUEE, FALSE, 0);
    SetStatus(status_text);
    SetTimer(main_window, ConversionTimerId, 250, nullptr);
    SetCancelButtonEnabled(true);

    conversion_thread = std::thread([input, output, target_format, output_existed]()
    {
        int status = XDVDFS_OPERATION_FAILED;
        std::string error;
        HMODULE library = LoadLibraryW(DolphinRvzDllPath().c_str());
        if (library != nullptr)
        {
            using ProgressCallback = int (*)(float, void*);
            using ConvertFunction = int (*)(const char*, const char*, int, ProgressCallback, void*, char*,
                unsigned int, unsigned int*);
            const auto convert = reinterpret_cast<ConvertFunction>(GetProcAddress(library, "DolphinDiscConvert"));
            if (convert != nullptr)
            {
                struct ProgressContext
                {
                    unsigned long long input_size;
                } progress_context{std::max<unsigned long long>(1, FileSize(input))};
                const auto report_progress = [](float percent, void* context) -> int
                {
                    auto* progress = static_cast<ProgressContext*>(context);
                    const unsigned long long completed = static_cast<unsigned long long>(
                        static_cast<double>(progress->input_size) * percent / 100.0);
                    completed_bytes.store(completed);
                    dolphin_progress_bytes.store(completed);
                    const int position = static_cast<int>((std::min)(percent, 99.0f));
                    dolphin_progress_percent.store(position);
                    PostMessageW(main_window, DolphinProgressMessage, 0, 0);
                    return cancel_requested.load() ? 0 : 1;
                };
                const std::string input_utf8 = ToUtf8(input);
                const std::string output_utf8 = ToUtf8(output);
                std::vector<char> diagnostic(4096, '\0');
                unsigned int notes = 0;
                const int result = convert(input_utf8.c_str(), output_utf8.c_str(), target_format,
                    report_progress, &progress_context, diagnostic.data(),
                    static_cast<unsigned int>(diagnostic.size()), &notes);
                dolphin_notes.store(notes);
                status = result == 0 ? XDVDFS_SUCCESS : XDVDFS_OPERATION_FAILED;
                if (result != 0)
                {
                    error = diagnostic.data();
                    if (error.empty())
                    {
                        error = cancel_requested.load()
                            ? "Conversion canceled by user"
                            : "Dolphin conversion failed with code " + std::to_string(result);
                    }
                }
            }
            else
            {
                error = "DolphinRVZ.dll is too old: it does not export DolphinDiscConvert";
            }
            FreeLibrary(library);
        }
        else
        {
            error = "Unable to load DolphinRVZ.dll (Windows error " + std::to_string(GetLastError()) + ")";
        }
        if (status != XDVDFS_SUCCESS && !output_existed)
        {
            DeleteFileW(output.c_str());
        }

        {
            std::lock_guard<std::mutex> lock(result_mutex);
            conversion_result = status;
            conversion_error = error;
        }
        conversion_active.store(false);
        PostMessageW(main_window, ConversionCompleteMessage, 0, 0);
    });
}

void StartChdmanConversion(
    const std::wstring& input,
    const wchar_t* command,
    const wchar_t* extension,
    const wchar_t* operation,
    bool extract_cd_to_iso = false)
{
    if (conversion_active.exchange(true))
    {
        SetStatus(L"A conversion is already running.");
        return;
    }
    if (conversion_thread.joinable())
    {
        conversion_thread.join();
    }

    const std::wstring output = OutputPathInDirectory(ReplaceExtension(input, extension), chdman_output_directory);
    std::wstring command_output = output;
    if (extract_cd_to_iso)
    {
        command_output = ReplaceExtension(output, L"cue");
    }
    total_bytes.store(std::max<unsigned long long>(1, DirectorySize(input)));
    conversion_unpack.store(false);
    conversion_kind.store(3);
    conversion_operation = operation;
    completed_bytes.store(0);
    conversion_output = output;
    conversion_start = std::chrono::steady_clock::now();
    SendMessageW(progress_control, PBM_SETPOS, 0, 0);
    SetStatus(std::wstring(operation) + L" ETA: calculating...");
    SetTimer(main_window, ConversionTimerId, 250, nullptr);
    SetCancelButtonEnabled(true);

    conversion_thread = std::thread([input, output, command_output, command, extract_cd_to_iso]()
    {
        int status = XDVDFS_OPERATION_FAILED;
        std::string error = "Unable to load chdman.dll";
        HMODULE library = LoadLibraryW(ChdmanDllPath().c_str());
        if (library != nullptr)
        {
            using ExecuteFunction = int (*)(int, const char *const *, char *, size_t);
            const auto execute = reinterpret_cast<ExecuteFunction>(GetProcAddress(library, "chdman_execute"));
            if (execute != nullptr)
            {
                std::vector<std::string> arguments{
                    "chdman", ToUtf8(command), "-i", ToUtf8(input), "-o", ToUtf8(command_output)};
                if (extract_cd_to_iso)
                {
                    arguments.emplace_back("-ob");
                    arguments.push_back(ToUtf8(output));
                }
                std::vector<const char *> argv;
                argv.reserve(arguments.size());
                for (const std::string& argument : arguments)
                {
                    argv.push_back(argument.c_str());
                }
                std::vector<char> diagnostic(4096, '\0');
                const int exit_code = execute(
                    static_cast<int>(argv.size()), argv.data(), diagnostic.data(), diagnostic.size());
                status = exit_code == 0 ? XDVDFS_SUCCESS : XDVDFS_OPERATION_FAILED;
                error = exit_code == 0 ? "" : std::string(diagnostic.data());
                if (error.empty() && exit_code != 0)
                {
                    error = "CHDMAN returned code " + std::to_string(exit_code);
                }
            }
            else
            {
                error = "chdman.dll does not export chdman_execute";
            }
            FreeLibrary(library);

            if (status == XDVDFS_SUCCESS && extract_cd_to_iso)
            {
                DeleteFileW(command_output.c_str());
            }
        }

        {
            std::lock_guard<std::mutex> lock(result_mutex);
            conversion_result = status;
            conversion_error = error;
        }
        conversion_active.store(false);
        PostMessageW(main_window, ConversionCompleteMessage, 0, 0);
    });
}

// ---------------------------------------------------------------------------
// ZAR (ZArchive) support. A ZAR stores the files of an Xbox 360 disc, not the
// disc image itself, so ISO -> ZAR -> XISO restores the same files but not a
// byte-identical image. Xenia mounts .zar archives directly.
// ---------------------------------------------------------------------------
constexpr wchar_t ZarFidelityNotice[] =
    L"ZAR stores the files from the disc image, not the image itself. The conversion is reversible "
    L"(ZAR to XISO restores all files), but the resulting image will NOT be byte-identical to the original input.";

std::string WindowsErrorText(const char* what, DWORD code)
{
    return std::string(what) + " (Windows error " + std::to_string(code) + ")";
}

struct ZarSink
{
    std::wstring path;
    HANDLE file = INVALID_HANDLE_VALUE;
    bool created = false;
    bool failed = false;
    DWORD error = 0;
};

void ZarOpenOutput(const int32_t part_index, void* context)
{
    auto* sink = static_cast<ZarSink*>(context);
    if (part_index != -1 || sink->file != INVALID_HANDLE_VALUE)
        return;
    sink->file = CreateFileW(sink->path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (sink->file == INVALID_HANDLE_VALUE)
    {
        sink->failed = true;
        sink->error = GetLastError();
        return;
    }
    sink->created = true;
}

void ZarWriteOutput(const void* data, size_t length, void* context)
{
    auto* sink = static_cast<ZarSink*>(context);
    const auto* bytes = static_cast<const unsigned char*>(data);
    while (!sink->failed && length > 0)
    {
        const DWORD chunk = static_cast<DWORD>((std::min<size_t>)(length, 16u * 1024 * 1024));
        DWORD written = 0;
        if (!WriteFile(sink->file, bytes, chunk, &written, nullptr) || written == 0)
        {
            sink->failed = true;
            sink->error = GetLastError();
            return;
        }
        bytes += written;
        length -= written;
    }
}

struct ZarPackContext
{
    ZArchiveWriter* writer = nullptr;
    ZarSink* sink = nullptr;
    std::string failure;
    unsigned long long written = 0;
    bool has_default_xex = false;
};

int ZarShouldStop(const ZarPackContext& context)
{
    return (cancel_requested.load() || context.sink->failed) ? 1 : 0;
}

int ZarOnStart(uint64_t payload_bytes, uint64_t, void* context)
{
    total_bytes.store((std::max<unsigned long long>)(1, payload_bytes));
    return ZarShouldStop(*static_cast<ZarPackContext*>(context));
}

int ZarOnDirectory(const char* path, void* context)
{
    auto& state = *static_cast<ZarPackContext*>(context);
    if (!state.writer->MakeDir(path, true))
    {
        state.failure = std::string("Unable to add folder \"") + path + "\" to the archive";
        return 1;
    }
    return ZarShouldStop(state);
}

int ZarOnFileBegin(const char* path, uint64_t, void* context)
{
    auto& state = *static_cast<ZarPackContext*>(context);
    const std::string_view view(path);
    const size_t slash = view.find_last_of('/');
    if (slash != std::string_view::npos && !state.writer->MakeDir(std::string(view.substr(0, slash)).c_str(), true))
    {
        state.failure = std::string("Unable to add the folder of \"") + path + "\" to the archive";
        return 1;
    }
    if (!state.writer->StartNewFile(path))
    {
        state.failure = std::string("Unable to add \"") + path + "\" to the archive (duplicate or conflicting name)";
        return 1;
    }
    if (_stricmp(path, "default.xex") == 0)
        state.has_default_xex = true;
    return ZarShouldStop(state);
}

int ZarOnFileData(const unsigned char* data, size_t length, void* context)
{
    auto& state = *static_cast<ZarPackContext*>(context);
    state.writer->AppendData(data, length);
    state.written += length;
    zar_progress_bytes.store(state.written);
    return ZarShouldStop(state);
}

bool IsReservedDeviceName(std::string stem)
{
    for (char& c : stem)
        c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    while (!stem.empty() && stem.back() == ' ')
        stem.pop_back();
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL")
        return true;
    return stem.size() == 4 && (stem.compare(0, 3, "COM") == 0 || stem.compare(0, 3, "LPT") == 0) &&
        stem[3] >= '1' && stem[3] <= '9';
}

// Names come from an archive that may not be trustworthy, so refuse anything that could
// escape the extraction folder or name a Windows device.
bool IsSafeArchiveName(std::string_view name)
{
    if (name.empty() || name == "." || name == ".." || name.back() == ' ' || name.back() == '.')
        return false;
    for (const unsigned char c : name)
    {
        if (c < 0x20 || strchr("\\/:*?\"<>|", c) != nullptr)
            return false;
    }
    return !IsReservedDeviceName(std::string(name.substr(0, name.find('.'))));
}

std::wstring ArchiveNameToWide(std::string_view name)
{
    UINT code_page = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    int length = MultiByteToWideChar(code_page, flags, name.data(), static_cast<int>(name.size()), nullptr, 0);
    if (length <= 0)
    {
        // ZArchive names may be Windows-1252 rather than UTF-8.
        code_page = 1252;
        flags = 0;
        length = MultiByteToWideChar(code_page, flags, name.data(), static_cast<int>(name.size()), nullptr, 0);
    }
    if (length <= 0)
        return {};
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(code_page, flags, name.data(), static_cast<int>(name.size()), &result[0], length);
    return result;
}

struct ZarExtractState
{
    unsigned long long total = 0;
    unsigned long long done = 0;
    bool canceled = false;
    std::string error;
    std::vector<char> buffer = std::vector<char>(1024 * 1024);
};

// Walks the archive. With a target folder it extracts every file; without one it only
// validates names and sums the file sizes.
bool ZarVisit(ZArchiveReader& reader, const std::string& relative, const std::filesystem::path* target,
    int depth, ZarExtractState& state)
{
    if (depth > 64)
    {
        state.error = "The archive folders are nested too deeply";
        return false;
    }
    const ZArchiveNodeHandle directory = reader.LookUp(relative, false, true);
    if (directory == ZARCHIVE_INVALID_NODE)
    {
        state.error = "The archive is damaged (missing folder \"" + relative + "\")";
        return false;
    }

    const uint32_t count = reader.GetDirEntryCount(directory);
    for (uint32_t index = 0; index < count; ++index)
    {
        ZArchiveReader::DirEntry entry{};
        if (!reader.GetDirEntry(directory, index, entry))
        {
            state.error = "The archive is damaged (unreadable folder entry)";
            return false;
        }
        if (!IsSafeArchiveName(entry.name))
        {
            state.error = "The archive contains an unsupported file name: " + std::string(entry.name);
            return false;
        }

        const std::string child_relative = relative.empty()
            ? std::string(entry.name)
            : relative + "/" + std::string(entry.name);
        std::filesystem::path child_path;
        if (target != nullptr)
            child_path = *target / ArchiveNameToWide(entry.name);

        if (entry.isDirectory)
        {
            if (target != nullptr && !CreateDirectoryW(child_path.c_str(), nullptr))
            {
                state.error = WindowsErrorText("Unable to create a temporary folder", GetLastError());
                return false;
            }
            if (!ZarVisit(reader, child_relative, target != nullptr ? &child_path : nullptr, depth + 1, state))
                return false;
            continue;
        }

        const ZArchiveNodeHandle node = reader.LookUp(child_relative, true, false);
        if (node == ZARCHIVE_INVALID_NODE || !reader.IsFile(node))
        {
            state.error = "The archive is damaged (missing file \"" + child_relative + "\")";
            return false;
        }
        const uint64_t size = reader.GetFileSize(node);
        if (target == nullptr)
        {
            state.total += size;
            continue;
        }

        HANDLE file = CreateFileW(child_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            state.error = WindowsErrorText("Unable to create a temporary file", GetLastError());
            return false;
        }
        uint64_t offset = 0;
        while (offset < size)
        {
            const uint64_t chunk = (std::min<uint64_t>)(state.buffer.size(), size - offset);
            DWORD written = 0;
            if (reader.ReadFromFile(node, offset, chunk, state.buffer.data()) != chunk)
            {
                state.error = "The archive is damaged (unable to read \"" + child_relative + "\")";
                CloseHandle(file);
                return false;
            }
            if (!WriteFile(file, state.buffer.data(), static_cast<DWORD>(chunk), &written, nullptr) ||
                written != chunk)
            {
                state.error = WindowsErrorText("Unable to write a temporary file", GetLastError());
                CloseHandle(file);
                return false;
            }
            offset += chunk;
            state.done += chunk;
            zar_progress_bytes.store(state.done);
            if (cancel_requested.load())
            {
                state.canceled = true;
                CloseHandle(file);
                return false;
            }
        }
        CloseHandle(file);
    }
    return true;
}

void FinishZarJob(int status, const std::string& error, const std::wstring& notice)
{
    {
        std::lock_guard<std::mutex> lock(result_mutex);
        conversion_result = status;
        conversion_error = error;
        conversion_notice = notice;
    }
    conversion_active.store(false);
    PostMessageW(main_window, ConversionCompleteMessage, 0, 0);
}

bool BeginZarJob(int kind, const std::wstring& output, const wchar_t* operation, unsigned long long expected_bytes)
{
    if (conversion_active.exchange(true))
    {
        SetStatus(L"A conversion is already running.");
        return false;
    }
    if (conversion_thread.joinable())
    {
        conversion_thread.join();
    }
    if (GetFileAttributesW(output.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        SetStatus(L"Output already exists; choose a different output folder or rename the existing file.");
        conversion_active.store(false);
        return false;
    }

    cancel_requested.store(false);
    cancel_current_operation = false;
    conversion_output = output;
    conversion_operation = operation;
    conversion_notice.clear();
    total_bytes.store((std::max<unsigned long long>)(1, expected_bytes));
    completed_bytes.store(0);
    zar_progress_bytes.store(0);
    zar_phase.store(1);
    conversion_unpack.store(false);
    conversion_kind.store(kind);
    conversion_start = std::chrono::steady_clock::now();
    SendMessageW(progress_control, PBM_SETPOS, 0, 0);
    SetStatus(std::wstring(operation) + L" ETA: calculating...");
    SetTimer(main_window, ConversionTimerId, 250, nullptr);
    SetCancelButtonEnabled(true);
    return true;
}

void StartIsoToZarConversion(const std::wstring& input)
{
    const std::wstring base = EndsWith(input, L".xiso.iso") ? StandardIsoPathForXiso(input) : input;
    const std::wstring output = OutputPathInDirectory(ReplaceExtension(base, L"zar"), xdvdfs_output_directory);
    if (!BeginZarJob(ConversionKindIsoToZar, output, L"Compressing Xbox 360 ISO to ZAR...", FileSize(input)))
        return;

    conversion_thread = std::thread([input, output]()
    {
        int status = XDVDFS_OPERATION_FAILED;
        std::string error;
        std::wstring notice;
        ZarSink sink;
        sink.path = output;
        try
        {
            ZarPackContext context;
            context.sink = &sink;
            auto writer = std::make_unique<ZArchiveWriter>(ZarOpenOutput, ZarWriteOutput, &sink);
            context.writer = writer.get();
            if (sink.failed)
            {
                error = WindowsErrorText("Unable to create the output file", sink.error);
            }
            else
            {
                const xdvdfs_walk_callbacks callbacks{ZarOnStart, ZarOnDirectory, ZarOnFileBegin, ZarOnFileData};
                std::vector<unsigned char> diagnostic(4096, 0);
                const std::string input_utf8 = ToUtf8(input);
                const int walk_status = xdvdfs_walk(
                    input_utf8.c_str(), &callbacks, &context, diagnostic.data(), diagnostic.size());
                if (walk_status == XDVDFS_SUCCESS)
                {
                    writer->Finalize();
                    if (!sink.failed && !FlushFileBuffers(sink.file))
                    {
                        sink.failed = true;
                        sink.error = GetLastError();
                    }
                    if (sink.failed)
                    {
                        error = WindowsErrorText("Unable to write the output file", sink.error);
                    }
                    else
                    {
                        status = XDVDFS_SUCCESS;
                        if (!context.has_default_xex)
                        {
                            notice = L"Warning: no default.xex was found at the root of the disc, so Xenia is unlikely "
                                L"to boot this archive (only Xbox 360 titles are supported, not original Xbox games).";
                        }
                    }
                }
                else if (sink.failed)
                {
                    error = WindowsErrorText("Unable to write the output file", sink.error);
                }
                else if (!context.failure.empty())
                {
                    error = context.failure;
                }
                else if (walk_status == XDVDFS_CANCELLED)
                {
                    status = XDVDFS_CANCELLED;
                    error = "Conversion canceled by user";
                }
                else
                {
                    error = reinterpret_cast<const char*>(diagnostic.data());
                    if (error.empty())
                        error = "Unable to read the Xbox disc image (code " + std::to_string(walk_status) + ")";
                }
            }
        }
        catch (const std::exception& exception)
        {
            error = std::string("ZAR conversion failed: ") + exception.what();
        }
        catch (...)
        {
            error = "ZAR conversion failed with an unexpected error";
        }

        if (sink.file != INVALID_HANDLE_VALUE)
            CloseHandle(sink.file);
        if (status != XDVDFS_SUCCESS && sink.created)
            DeleteFileW(output.c_str());
        FinishZarJob(status, error, notice);
    });
}

void StartZarToXisoConversion(const std::wstring& input)
{
    const std::wstring output = OutputPathInDirectory(ReplaceExtension(input, L"xiso.iso"), xdvdfs_output_directory);
    if (!BeginZarJob(ConversionKindZarToXiso, output, L"Converting ZAR to XISO...", FileSize(input)))
        return;

    conversion_thread = std::thread([input, output]()
    {
        int status = XDVDFS_OPERATION_FAILED;
        std::string error;
        const std::wstring temporary = output + L".extract.tmp";
        bool temporary_created = false;
        bool output_created = false;
        try
        {
            ZarExtractState state;
            std::unique_ptr<ZArchiveReader> reader(ZArchiveReader::OpenFromFile(std::filesystem::path(input)));
            if (!reader)
            {
                error = "The file is not a valid ZAR archive";
            }
            else if (!ZarVisit(*reader, std::string(), nullptr, 0, state))
            {
                error = state.error;
            }
            else if (!CreateDirectoryW(temporary.c_str(), nullptr))
            {
                error = WindowsErrorText("Unable to create the temporary extraction folder", GetLastError());
            }
            else
            {
                temporary_created = true;
                total_bytes.store((std::max<unsigned long long>)(1, state.total));
                const std::filesystem::path temporary_path(temporary);
                if (!ZarVisit(*reader, std::string(), &temporary_path, 0, state))
                {
                    if (state.canceled)
                    {
                        status = XDVDFS_CANCELLED;
                        error = "Conversion canceled by user";
                    }
                    else
                    {
                        error = state.error;
                    }
                }
                else
                {
                    reader.reset();
                    zar_phase.store(2);
                    output_created = true;
                    std::vector<unsigned char> diagnostic(4096, 0);
                    const std::string temporary_utf8 = ToUtf8(temporary);
                    const std::string output_utf8 = ToUtf8(output);
                    const int pack_status = xdvdfs_pack(
                        temporary_utf8.c_str(), output_utf8.c_str(), diagnostic.data(), diagnostic.size());
                    if (pack_status == XDVDFS_SUCCESS)
                    {
                        status = XDVDFS_SUCCESS;
                    }
                    else
                    {
                        error = reinterpret_cast<const char*>(diagnostic.data());
                        if (error.empty())
                            error = "Unable to build the XISO image (code " + std::to_string(pack_status) + ")";
                    }
                }
            }
        }
        catch (const std::exception& exception)
        {
            error = std::string("ZAR conversion failed: ") + exception.what();
        }
        catch (...)
        {
            error = "ZAR conversion failed with an unexpected error";
        }

        if (temporary_created)
        {
            std::error_code ignored;
            std::filesystem::remove_all(std::filesystem::path(temporary), ignored);
        }
        if (status != XDVDFS_SUCCESS && output_created)
            DeleteFileW(output.c_str());
        FinishZarJob(status, error, std::wstring());
    });
}

void HandleDrop(HDROP drop)
{
    if (conversion_active.load() || batch_running)
    {
        SetStatus(L"A conversion is already running.");
        DragFinish(drop);
        return;
    }

    const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    if (count > 0)
    {
        const UINT length = DragQueryFileW(drop, 0, nullptr, 0);
        std::wstring path(static_cast<size_t>(length) + 1, L'\0');
        DragQueryFileW(drop, 0, &path[0], length + 1);
        path.resize(length);
        input_path = path;
        selected_input_paths = {path};
        RefreshSelection();
        SetStatus(L"Selected: " + input_path + L". Choose an operation.");
    }
    DragFinish(drop);
}

bool IsGameCubeImageInput(const std::wstring& input)
{
    return EndsWith(input, L".iso") || EndsWith(input, L".gcm");
}

bool DispatchOperation(UINT_PTR operation_id, const std::wstring& input)
{
    switch (operation_id)
    {
    case PackButtonId:
        StartConversion(input);
        break;
    case UnpackButtonId:
        if (!EndsWith(input, L".xiso.iso"))
        {
            SetStatus(L"XISO to folder requires a .xiso.iso input.");
            return false;
        }
        StartConversion(input);
        break;
    case XisoToIsoButtonId:
        if (!EndsWith(input, L".xiso.iso"))
        {
            SetStatus(L"XISO to ISO requires a .xiso.iso input.");
            return false;
        }
        StartConversion(input, true);
        break;
    case CreateCdButtonId:
        if (!EndsWith(input, L".cue") && !EndsWith(input, L".gdi") && !EndsWith(input, L".iso"))
        {
            SetStatus(L"Create CD CHD requires a .cue, .gdi, or .iso input.");
            return false;
        }
        StartChdmanConversion(input, L"createcd", L"chd", L"Creating CD CHD...");
        break;
    case CreateDvdButtonId:
        if (!EndsWith(input, L".iso"))
        {
            SetStatus(L"Create DVD CHD requires an .iso input.");
            return false;
        }
        StartChdmanConversion(input, L"createdvd", L"chd", L"Creating DVD CHD...");
        break;
    case ExtractCdButtonId:
        if (!EndsWith(input, L".chd"))
        {
            SetStatus(L"Extract CD requires a .chd input.");
            return false;
        }
        StartChdmanConversion(input, L"extractcd", L"cue", L"Extracting CD...");
        break;
    case ExtractDvdButtonId:
        if (!EndsWith(input, L".chd"))
        {
            SetStatus(L"Extract DVD requires a .chd input.");
            return false;
        }
        StartChdmanConversion(input, L"extractdvd", L"iso", L"Extracting DVD...");
        break;
    case ExtractCdToIsoButtonId:
        if (!EndsWith(input, L".chd"))
        {
            SetStatus(L"Extract CD to ISO requires a .chd input.");
            return false;
        }
        StartChdmanConversion(input, L"extractcd", L"iso", L"Extracting CD to ISO...", true);
        break;
    case IsoToRvzButtonId:
        if (!IsGameCubeImageInput(input))
        {
            SetStatus(L"ISO to RVZ requires a GameCube/Wii .iso or .gcm input.");
            return false;
        }
        StartDolphinConversion(input, DolphinFormatRvz);
        break;
    case RvzToIsoButtonId:
        if (!EndsWith(input, L".rvz"))
        {
            SetStatus(L"RVZ to ISO requires a .rvz input.");
            return false;
        }
        StartDolphinConversion(input, DolphinFormatIso);
        break;
    case IsoToWiaButtonId:
        if (!IsGameCubeImageInput(input))
        {
            SetStatus(L"ISO to WIA requires a GameCube/Wii .iso or .gcm input.");
            return false;
        }
        StartDolphinConversion(input, DolphinFormatWia);
        break;
    case WiaToIsoButtonId:
        if (!EndsWith(input, L".wia"))
        {
            SetStatus(L"WIA to ISO requires a .wia input.");
            return false;
        }
        StartDolphinConversion(input, DolphinFormatIso);
        break;
    case IsoToGczButtonId:
        if (!IsGameCubeImageInput(input))
        {
            SetStatus(L"ISO to GCZ requires a GameCube/Wii .iso or .gcm input.");
            return false;
        }
        StartDolphinConversion(input, DolphinFormatGcz);
        break;
    case GczToIsoButtonId:
        if (!EndsWith(input, L".gcz"))
        {
            SetStatus(L"GCZ to ISO requires a .gcz input.");
            return false;
        }
        StartDolphinConversion(input, DolphinFormatIso);
        break;
    case IsoToZarButtonId:
        if (!EndsWith(input, L".iso"))
        {
            SetStatus(L"ISO to ZAR requires an Xbox 360 .iso input.");
            return false;
        }
        StartIsoToZarConversion(input);
        break;
    case ZarToXisoButtonId:
        if (!EndsWith(input, L".zar"))
        {
            SetStatus(L"ZAR to XISO requires a .zar input.");
            return false;
        }
        StartZarToXisoConversion(input);
        break;
    default:
        return false;
    }
    return conversion_active.load();
}

void StartNextBatchItem()
{
    while (batch_running && !batch_stop_requested && batch_input_index < batch_input_paths.size())
    {
        input_path = batch_input_paths[batch_input_index];
        const std::wstring current_input = input_path;
        if (DispatchOperation(batch_operation_id, current_input))
        {
            SetStatus(L"Processing file " + std::to_wstring(batch_input_index + 1) + L" of " +
                std::to_wstring(batch_input_paths.size()) + L": " + current_input);
            return;
        }

        wchar_t status[1024]{};
        GetWindowTextW(status_control, status, ARRAYSIZE(status));
        batch_errors.push_back(current_input + L": " + status);
        ++batch_input_index;
    }

    if (batch_running)
    {
        batch_running = false;
        const size_t total = batch_input_paths.size();
        const size_t successes = batch_success_count;
        const std::vector<std::wstring> errors = std::move(batch_errors);
        batch_input_paths.clear();
        batch_input_index = 0;
        batch_success_count = 0;

        const bool stopped = batch_stop_requested;
        std::wstring summary = stopped ? L"Batch stopped: " : L"Batch complete: ";
        summary += std::to_wstring(successes) + L" of " +
            std::to_wstring(total) + L" file(s) converted successfully.";
        if (!errors.empty())
        {
            summary += L"\n\n";
            const size_t displayed_errors = (std::min)(errors.size(), size_t{8});
            for (size_t i = 0; i < displayed_errors; ++i)
                summary += errors[i] + L"\n";
            if (errors.size() > displayed_errors)
                summary += L"... and " + std::to_wstring(errors.size() - displayed_errors) + L" more.";
        }
        SetStatus((stopped ? L"Batch stopped: " : L"Batch complete: ") +
            std::to_wstring(successes) + L" of " +
            std::to_wstring(total) + L" file(s) converted successfully.");
        MessageBoxW(main_window, summary.c_str(), L"Batch conversion", MB_OK |
            (errors.empty() ? MB_ICONINFORMATION : MB_ICONWARNING));
    }
}

bool CurrentOperationInterruptible()
{
    switch (conversion_kind.load())
    {
    case 5:
    case ConversionKindIsoToZar:
        return true;
    case ConversionKindZarToXiso:
        // Once the XISO is being built there is no way to stop the packer.
        return zar_phase.load() == 1;
    default:
        return false;
    }
}

bool ConfirmZarFidelity(bool to_zar)
{
    const std::wstring message = to_zar
        ? std::wstring(L"ZAR keeps the files of the disc, not the disc image itself.\n\n"
            L"\u2022 The conversion is reversible: ZAR to XISO restores every file.\n"
            L"\u2022 The restored image will NOT be byte-identical to the original ISO "
            L"(padding, video partition, sector layout and security data are not kept).\n"
            L"\u2022 ZAR is meant for Xbox 360 titles and Xenia; original Xbox games are not supported.\n\n"
            L"Continue?")
        : std::wstring(L"This builds a new XISO from the files stored in the ZAR archive.\n\n"
            L"\u2022 All files are restored, so the conversion is reversible in content.\n"
            L"\u2022 The image will NOT be byte-identical to the original ISO that the ZAR was made from.\n"
            L"\u2022 The files are first extracted to a temporary folder next to the output, "
            L"so free disk space for the full game is required while it runs.\n\n"
            L"Continue?");
    return MessageBoxW(main_window, message.c_str(),
        to_zar ? L"Convert ISO to ZAR" : L"Convert ZAR to XISO", MB_OKCANCEL | MB_ICONINFORMATION) == IDOK;
}

bool ConfirmDolphinFidelity(bool to_wia)
{
    const std::wstring format = to_wia ? L"WIA" : L"GCZ";
    const std::wstring message =
        L"Converting to " + format + L" is designed to be lossless: converting the file back to ISO "
        L"restores the original image bit for bit when the source is an unmodified, full disc dump.\n\n"
        L"This is NOT guaranteed in every case:\n"
        L"\u2022 Scrubbed or trimmed images, and NKit images, no longer contain the complete original disc. "
        L"They are stored as they are and the removed data cannot be brought back.\n"
        L"\u2022 WBFS and CISO inputs are not accepted; convert them to a plain ISO first.\n"
        + (to_wia
            ? std::wstring(L"\u2022 WIA is the older Dolphin format. RVZ compresses better and is the better choice unless "
                L"you need WIA for compatibility with other tools.\n")
            : std::wstring(L"\u2022 GCZ saves little space on Wii discs because their data is encrypted. "
                L"It is mainly useful for GameCube images.\n"
                L"\u2022 Very old Dolphin versions (before 5.0-11893) may refuse some GCZ files.\n"))
        + L"\nThe round trip was verified on a GameCube image only. Continue?";
    return MessageBoxW(main_window, message.c_str(),
        to_wia ? L"Convert ISO to WIA" : L"Convert ISO to GCZ", MB_OKCANCEL | MB_ICONINFORMATION) == IDOK;
}

void BeginOperation(UINT_PTR operation_id)
{
    std::vector<std::wstring> inputs = selected_input_paths;
    if (inputs.empty() && !input_path.empty())
        inputs.push_back(input_path);
    if (inputs.empty())
    {
        SetStatus(L"Choose or drop an input file first.");
        return;
    }

    if (operation_id == IsoToZarButtonId || operation_id == ZarToXisoButtonId)
    {
        if (conversion_active.load() || batch_running)
        {
            SetStatus(L"A conversion is already running.");
            return;
        }
        if (!ConfirmZarFidelity(operation_id == IsoToZarButtonId))
        {
            SetStatus(L"ZAR conversion canceled.");
            return;
        }
    }

    if (operation_id == IsoToWiaButtonId || operation_id == IsoToGczButtonId)
    {
        if (conversion_active.load() || batch_running)
        {
            SetStatus(L"A conversion is already running.");
            return;
        }
        if (!ConfirmDolphinFidelity(operation_id == IsoToWiaButtonId))
        {
            SetStatus(L"Conversion canceled.");
            return;
        }
    }

    if (inputs.size() == 1)
    {
        batch_running = false;
        DispatchOperation(operation_id, inputs.front());
        return;
    }

    if (conversion_active.load() || batch_running)
    {
        SetStatus(L"A conversion is already running.");
        return;
    }

    batch_input_paths = std::move(inputs);
    batch_errors.clear();
    batch_input_index = 0;
    batch_success_count = 0;
    batch_stop_requested = false;
    batch_operation_id = operation_id;
    batch_running = true;
    StartNextBatchItem();
}

void RequestCancelOperation(HWND window)
{
    if (!conversion_active.load())
    {
        SetStatus(L"No conversion is currently running.");
        return;
    }

    const bool rvz_operation = CurrentOperationInterruptible();
    const wchar_t* prompt = rvz_operation
        ? L"Cancel the current conversion and stop the remaining batch?"
        : L"This operation cannot interrupt the current file safely. Let it finish and stop any remaining batch files?";
    if (MessageBoxW(window, prompt, L"Confirm cancellation", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        return;

    if (!conversion_active.load())
    {
        SetStatus(L"The operation completed before cancellation was accepted.");
        return;
    }

    batch_stop_requested = true;
    cancel_current_operation = true;
    if (rvz_operation)
        cancel_requested.store(true);
    SetCancelButtonEnabled(false);
    SetStatus(rvz_operation
        ? L"Cancellation requested..."
        : L"The current file is finishing; remaining batch items will be skipped.");
}

// ---------------------------------------------------------------------------
// Modern UI layer: DPI-aware, custom-drawn and light/dark themed. Everything is
// painted by the application so the look is consistent across Windows versions.
// ---------------------------------------------------------------------------
struct Palette
{
    COLORREF window, card, card_border, text, text_secondary;
    COLORREF accent, accent_hover, accent_pressed, on_accent;
    COLORREF button_fill, button_hover, button_pressed, button_border;
    COLORREF disabled_text, disabled_fill, danger, success, track, drop_fill;
};

const Palette LightPalette{
    RGB(244, 245, 247), RGB(255, 255, 255), RGB(226, 229, 234), RGB(28, 32, 38), RGB(99, 107, 119),
    RGB(47, 111, 237), RGB(36, 93, 214), RGB(28, 76, 180), RGB(255, 255, 255),
    RGB(243, 244, 246), RGB(231, 234, 239), RGB(220, 224, 230), RGB(214, 219, 226),
    RGB(160, 167, 177), RGB(238, 240, 243), RGB(200, 45, 45), RGB(22, 128, 60), RGB(228, 231, 236),
    RGB(246, 249, 255)};

const Palette DarkPalette{
    RGB(22, 24, 29), RGB(31, 34, 41), RGB(46, 50, 59), RGB(233, 235, 239), RGB(152, 159, 171),
    RGB(96, 145, 255), RGB(124, 165, 255), RGB(74, 120, 230), RGB(10, 16, 32),
    RGB(42, 46, 56), RGB(52, 57, 69), RGB(62, 68, 82), RGB(58, 63, 75),
    RGB(104, 110, 121), RGB(34, 37, 45), RGB(255, 120, 120), RGB(88, 204, 128), RGB(48, 52, 62),
    RGB(28, 34, 48)};

enum class ButtonKind { Primary, Secondary, Ghost, Danger };

struct ButtonInfo
{
    HWND hwnd;
    UINT_PTR id;
    ButtonKind kind;
    bool on_card;
};

struct OperationSpec
{
    UINT_PTR id;
    const wchar_t* label;
};

struct Card
{
    const wchar_t* title = L"";
    const wchar_t* caption = L"";
    std::vector<OperationSpec> operations;
    UINT_PTR change_id = 0;
    UINT_PTR reset_id = 0;
    const std::wstring* folder = nullptr;
    RECT rc{}, title_rc{}, caption_rc{}, folder_rc{};
};

const Palette* palette = &LightPalette;
bool dark_mode = false;
UINT current_dpi = 96;
HBRUSH card_brush = nullptr;
HFONT font_body = nullptr;
HFONT font_small = nullptr;
HFONT font_card = nullptr;
HFONT font_title = nullptr;
HFONT font_button = nullptr;
HWND hover_button = nullptr;
HWND tooltip_control = nullptr;
std::vector<ButtonInfo> ui_buttons;
std::vector<Card> cards;
RECT header_title_rc{}, header_subtitle_rc{}, drop_rc{}, drop_icon_rc{}, drop_title_rc{}, drop_detail_rc{}, footer_rc{};
int min_content_height = 0;

int S(int value)
{
    return MulDiv(value, static_cast<int>(current_dpi), 96);
}

Gdiplus::Color GdiColor(COLORREF color, BYTE alpha = 255)
{
    return Gdiplus::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color));
}

COLORREF Blend(COLORREF from, COLORREF to, int percent)
{
    auto mix = [percent](int a, int b) { return a + (b - a) * percent / 100; };
    return RGB(mix(GetRValue(from), GetRValue(to)), mix(GetGValue(from), GetGValue(to)),
        mix(GetBValue(from), GetBValue(to)));
}

void AddRoundedRect(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& r, float radius)
{
    float d = (std::min)({radius * 2.0f, r.Width, r.Height});
    if (d < 1.0f)
        d = 1.0f;
    path.AddArc(r.X, r.Y, d, d, 180.0f, 90.0f);
    path.AddArc(r.GetRight() - d, r.Y, d, d, 270.0f, 90.0f);
    path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0.0f, 90.0f);
    path.AddArc(r.X, r.GetBottom() - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

void FillRounded(Gdiplus::Graphics& g, const RECT& rc, int radius, COLORREF color, BYTE alpha = 255)
{
    Gdiplus::GraphicsPath path;
    AddRoundedRect(path, Gdiplus::RectF(static_cast<float>(rc.left), static_cast<float>(rc.top),
        static_cast<float>(rc.right - rc.left), static_cast<float>(rc.bottom - rc.top)), static_cast<float>(radius));
    Gdiplus::SolidBrush brush(GdiColor(color, alpha));
    g.FillPath(&brush, &path);
}

void StrokeRounded(Gdiplus::Graphics& g, const RECT& rc, int radius, COLORREF color, float width, bool dashed = false)
{
    const float half = width / 2.0f;
    Gdiplus::GraphicsPath path;
    AddRoundedRect(path, Gdiplus::RectF(rc.left + half, rc.top + half,
        static_cast<float>(rc.right - rc.left) - width, static_cast<float>(rc.bottom - rc.top) - width),
        static_cast<float>(radius) - half);
    Gdiplus::Pen pen(GdiColor(color), width);
    if (dashed)
        pen.SetDashStyle(Gdiplus::DashStyleDash);
    g.DrawPath(&pen, &path);
}

HFONT MakeFont(int tenths_of_point, int weight)
{
    return CreateFontW(-MulDiv(tenths_of_point, static_cast<int>(current_dpi), 720), 0, 0, 0, weight,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void RebuildFonts()
{
    for (HFONT* font : {&font_body, &font_small, &font_card, &font_title, &font_button})
    {
        if (*font != nullptr)
            DeleteObject(*font);
    }
    font_body = MakeFont(100, FW_NORMAL);
    font_small = MakeFont(90, FW_NORMAL);
    font_card = MakeFont(115, FW_SEMIBOLD);
    font_title = MakeFont(200, FW_BOLD);
    font_button = MakeFont(95, FW_SEMIBOLD);
    if (status_control != nullptr)
        SendMessageW(status_control, WM_SETFONT, reinterpret_cast<WPARAM>(font_body), TRUE);
}

bool SystemUsesDarkMode()
{
    DWORD value = 1;
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS)
    {
        return value == 0;
    }
    return false;
}

void ApplyTheme(HWND window)
{
    dark_mode = SystemUsesDarkMode();
    palette = dark_mode ? &DarkPalette : &LightPalette;
    if (card_brush != nullptr)
        DeleteObject(card_brush);
    card_brush = CreateSolidBrush(palette->card);
    const BOOL use_dark = dark_mode ? TRUE : FALSE;
    DwmSetWindowAttribute(window, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &use_dark, sizeof(use_dark));
    if (tooltip_control != nullptr)
        SetWindowTheme(tooltip_control, dark_mode ? L"DarkMode_Explorer" : nullptr, nullptr);
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

std::wstring FormatBytes(unsigned long long bytes)
{
    static const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4)
    {
        value /= 1024.0;
        ++unit;
    }
    wchar_t buffer[48]{};
    swprintf(buffer, ARRAYSIZE(buffer), unit == 0 ? L"%.0f %s" : L"%.2f %s", value, units[unit]);
    return buffer;
}

std::wstring FileNameOf(const std::wstring& path)
{
    const size_t separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? path : path.substr(separator + 1);
}

std::wstring DirectoryOf(const std::wstring& path)
{
    const size_t separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? std::wstring() : path.substr(0, separator);
}

void SelectionText(std::wstring& title, std::wstring& detail)
{
    std::vector<std::wstring> files = selected_input_paths;
    if (files.empty() && !input_path.empty())
        files.push_back(input_path);

    if (files.empty())
    {
        title = L"Drop a disc image here";
        detail = L"ISO, GCM, XISO, CUE/GDI, CHD, ZAR, RVZ, WIA and GCZ are supported. Drop one file, or choose several for a batch.";
    }
    else if (files.size() == 1)
    {
        title = FileNameOf(files.front());
        detail = FormatBytes(selection_bytes) + L"   \u00B7   " + DirectoryOf(files.front());
    }
    else
    {
        title = std::to_wstring(files.size()) + L" files selected";
        detail = FormatBytes(selection_bytes) + L"   \u00B7   " + FileNameOf(files[0]) + L", " + FileNameOf(files[1]);
        if (files.size() > 2)
            detail += L" and " + std::to_wstring(files.size() - 2) + L" more";
    }
}

void RefreshSelection()
{
    selection_bytes = 0;
    if (selected_input_paths.empty() && !input_path.empty())
        selection_bytes = FileSize(input_path);
    for (const std::wstring& path : selected_input_paths)
        selection_bytes += FileSize(path);
    if (main_window != nullptr)
        InvalidateRect(main_window, &drop_rc, FALSE);
}

void UpdateControlStates(HWND window)
{
    hover_button = nullptr;
    auto set_enabled = [window](UINT_PTR id, bool enabled)
    {
        if (HWND control = GetDlgItem(window, static_cast<int>(id)))
            EnableWindow(control, enabled ? TRUE : FALSE);
    };
    set_enabled(BrowseFilesButtonId, !ui_busy);
    for (const Card& card : cards)
    {
        for (const OperationSpec& operation : card.operations)
            set_enabled(operation.id, !ui_busy);
        set_enabled(card.change_id, !ui_busy);
        set_enabled(card.reset_id, !ui_busy && card.folder != nullptr && !card.folder->empty());
    }
}

void SetOperationsEnabled(bool busy)
{
    ui_busy = busy;
    if (main_window != nullptr)
        UpdateControlStates(main_window);
}

void UpdateFolderUi()
{
    if (main_window == nullptr)
        return;
    UpdateControlStates(main_window);
    InvalidateRect(main_window, nullptr, FALSE);
}

const ButtonInfo* FindButton(HWND hwnd)
{
    for (const ButtonInfo& info : ui_buttons)
    {
        if (info.hwnd == hwnd)
            return &info;
    }
    return nullptr;
}

void DrawUiButton(const DRAWITEMSTRUCT& item)
{
    const ButtonInfo* info = FindButton(item.hwndItem);
    if (info == nullptr)
        return;

    const Palette& p = *palette;
    const int width = item.rcItem.right - item.rcItem.left;
    const int height = item.rcItem.bottom - item.rcItem.top;
    const RECT local{0, 0, width, height};

    HDC dc = CreateCompatibleDC(item.hDC);
    HBITMAP bitmap = CreateCompatibleBitmap(item.hDC, width, height);
    HGDIOBJ previous_bitmap = SelectObject(dc, bitmap);

    HBRUSH background = CreateSolidBrush(info->on_card ? p.card : p.window);
    FillRect(dc, &local, background);
    DeleteObject(background);

    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0 && !disabled;
    const bool hot = hover_button == item.hwndItem && !disabled;
    const bool focused = (item.itemState & ODS_FOCUS) != 0 && (item.itemState & ODS_NOFOCUSRECT) == 0;
    const COLORREF surface = info->on_card ? p.card : p.window;

    COLORREF fill = p.button_fill;
    COLORREF border = p.button_border;
    COLORREF text = p.text;
    bool draw_fill = true;
    bool draw_border = true;

    switch (info->kind)
    {
    case ButtonKind::Primary:
        fill = pressed ? p.accent_pressed : (hot ? p.accent_hover : p.accent);
        text = p.on_accent;
        draw_border = false;
        if (disabled)
        {
            fill = Blend(p.accent, surface, 55);
            text = Blend(p.on_accent, surface, 25);
        }
        break;
    case ButtonKind::Ghost:
        fill = pressed ? p.button_pressed : p.button_hover;
        draw_fill = pressed || hot;
        draw_border = false;
        break;
    case ButtonKind::Danger:
        fill = pressed ? p.button_pressed : (hot ? p.button_hover : p.button_fill);
        text = p.danger;
        break;
    default:
        fill = pressed ? p.button_pressed : (hot ? p.button_hover : p.button_fill);
        break;
    }
    if (disabled && info->kind != ButtonKind::Primary)
    {
        fill = p.disabled_fill;
        text = p.disabled_text;
        draw_fill = info->kind != ButtonKind::Ghost;
    }

    {
        Gdiplus::Graphics graphics(dc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const int radius = S(8);
        if (draw_fill)
            FillRounded(graphics, local, radius, fill);
        if (draw_border && !disabled)
            StrokeRounded(graphics, local, radius, border, 1.0f);
        if (focused)
        {
            RECT ring = local;
            InflateRect(&ring, -S(1), -S(1));
            StrokeRounded(graphics, ring, radius, p.accent, static_cast<float>((std::max)(2, S(2))));
        }
    }

    wchar_t label[128]{};
    GetWindowTextW(item.hwndItem, label, ARRAYSIZE(label));
    SelectObject(dc, font_button);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, text);
    RECT text_rc = local;
    DrawTextW(dc, label, -1, &text_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    BitBlt(item.hDC, item.rcItem.left, item.rcItem.top, width, height, dc, 0, 0, SRCCOPY);
    SelectObject(dc, previous_bitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

LRESULT CALLBACK ButtonSubclassProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR)
{
    switch (message)
    {
    case WM_MOUSEMOVE:
        if (hover_button != hwnd)
        {
            hover_button = hwnd;
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&track);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        break;
    case WM_MOUSELEAVE:
        if (hover_button == hwnd)
            hover_button = nullptr;
        InvalidateRect(hwnd, nullptr, FALSE);
        break;
    case WM_SETCURSOR:
        if (IsWindowEnabled(hwnd))
        {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, ButtonSubclassProc, 1);
        break;
    }
    return DefSubclassProc(hwnd, message, wparam, lparam);
}

const wchar_t* TooltipFor(UINT_PTR id)
{
    switch (id)
    {
    case BrowseFilesButtonId:
        return L"Select one or more disc image files. Choosing several lets you run the same operation on all of them as a batch. You can also drop a single file onto the window.";
    case PackButtonId:
        return L"Convert a standard Xbox ISO into a compact XISO. The result is saved as the input name plus .xiso.iso.";
    case XisoToIsoButtonId:
        return L"Convert a .xiso.iso image back into a standard ISO, padded to the size of a dual-layer DVD.";
    case UnpackButtonId:
        return L"Extract all files from a .xiso.iso image into a folder.";
    case IsoToZarButtonId:
        return L"Compress an Xbox 360 .iso (any XGD layout) into a .zar archive that Xenia can load directly. ZAR keeps the files, not the image: it can be converted back with ZAR to XISO, but the result is not byte-identical to the original. This can be cancelled while running.";
    case ZarToXisoButtonId:
        return L"Rebuild a .zar archive as a .xiso.iso image. All files are restored, but the image is not byte-identical to the ISO the archive was made from. Needs temporary disk space for the extracted files.";
    case CreateCdButtonId:
        return L"Compress a CD image (.cue, .gdi or .iso) into a CHD file.";
    case CreateDvdButtonId:
        return L"Compress a DVD .iso image into a CHD file.";
    case ExtractCdButtonId:
        return L"Extract a CD .chd file back to a .cue sheet with its data files.";
    case ExtractDvdButtonId:
        return L"Extract a DVD .chd file back to a single .iso image.";
    case ExtractCdToIsoButtonId:
        return L"Extract a CD .chd file to a single .iso image. The temporary .cue sheet is removed afterwards.";
    case IsoToRvzButtonId:
        return L"Compress a GameCube or Wii .iso or .gcm image into a space-saving .rvz file. This can be cancelled while running.";
    case RvzToIsoButtonId:
        return L"Restore a GameCube or Wii .rvz file to a full .iso image.";
    case IsoToWiaButtonId:
        return L"Compress a GameCube or Wii .iso or .gcm image into a .wia file (older Dolphin format; RVZ compresses better). Designed to be lossless, but not guaranteed for scrubbed or NKit images. You are asked to confirm first.";
    case WiaToIsoButtonId:
        return L"Restore a GameCube or Wii .wia file to a full .iso image.";
    case IsoToGczButtonId:
        return L"Compress a GameCube or Wii .iso or .gcm image into a .gcz file. Works well for GameCube; saves little on Wii discs. Designed to be lossless, but not guaranteed for scrubbed or NKit images. You are asked to confirm first.";
    case GczToIsoButtonId:
        return L"Restore a GameCube or Wii .gcz file to a full .iso image.";
    case PickXdvdfsFolderButtonId:
    case PickChdmanFolderButtonId:
    case PickDolphinFolderButtonId:
        return L"Choose the folder where results from this tool are saved. By default they are written next to the input file.";
    case ResetXdvdfsFolderButtonId:
    case ResetChdmanFolderButtonId:
    case ResetDolphinFolderButtonId:
        return L"Go back to saving results next to the input file.";
    case CancelOperationButtonId:
        return L"Stop the running job and skip the remaining files of a batch. ISO to ZAR, ZAR extraction and GameCube/Wii (Dolphin) jobs stop right away; other Xbox and CHD jobs finish the current file first.";
    case ReadmeMenuItemId:
        return L"Open the README with usage notes and build details.";
    case AboutMenuItemId:
        return L"Show a short description of emucompress.";
    default:
        return nullptr;
    }
}

void CreateTooltipControl(HWND parent)
{
    tooltip_control = CreateWindowExW(
        WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, parent, nullptr, nullptr, nullptr);
    if (tooltip_control == nullptr)
        return;
    SendMessageW(tooltip_control, TTM_SETDELAYTIME, TTDT_INITIAL, 400);
    SendMessageW(tooltip_control, TTM_SETDELAYTIME, TTDT_AUTOPOP, 15000);
    SendMessageW(tooltip_control, TTM_SETMAXTIPWIDTH, 0, S(320));
    SetWindowTheme(tooltip_control, dark_mode ? L"DarkMode_Explorer" : nullptr, nullptr);
}

void AddTooltip(HWND parent, HWND control, UINT_PTR id)
{
    const wchar_t* text = TooltipFor(id);
    if (tooltip_control == nullptr || text == nullptr)
        return;
    TOOLINFOW tool{};
    tool.cbSize = sizeof(tool);
    tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    tool.hwnd = parent;
    tool.uId = reinterpret_cast<UINT_PTR>(control);
    tool.lpszText = const_cast<LPWSTR>(text);
    SendMessageW(tooltip_control, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
}

HWND CreateUiButton(HWND parent, UINT_PTR id, const wchar_t* text, ButtonKind kind, bool on_card)
{
    HWND button = CreateWindowExW(
        0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    SetWindowSubclass(button, ButtonSubclassProc, 1, 0);
    ui_buttons.push_back({button, id, kind, on_card});
    AddTooltip(parent, button, id);
    return button;
}

LRESULT CALLBACK ProgressWindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case PBM_SETRANGE:
    case PBM_SETMARQUEE:
        return 0;
    case PBM_SETPOS:
    {
        const LONG_PTR previous = GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        const LONG_PTR position = (std::min<LONG_PTR>)(100, (std::max<LONG_PTR>)(0, static_cast<LONG_PTR>(wparam)));
        if (position != previous)
        {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, position);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return previous;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps{};
        HDC target = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        const int width = rc.right;
        const int height = rc.bottom;
        HDC dc = CreateCompatibleDC(target);
        HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
        HGDIOBJ previous_bitmap = SelectObject(dc, bitmap);
        HBRUSH background = CreateSolidBrush(palette->card);
        FillRect(dc, &rc, background);
        DeleteObject(background);
        {
            Gdiplus::Graphics graphics(dc);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            FillRounded(graphics, rc, height / 2, palette->track);
            const LONG_PTR position = GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            if (position > 0)
            {
                RECT fill = rc;
                fill.right = (std::max<LONG>)(height, static_cast<LONG>(width * position / 100));
                FillRounded(graphics, fill, height / 2, palette->accent);
            }
        }
        BitBlt(target, 0, 0, width, height, dc, 0, 0, SRCCOPY);
        SelectObject(dc, previous_bitmap);
        DeleteObject(bitmap);
        DeleteDC(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

void DrawLabel(HDC dc, const std::wstring& text, RECT rc, HFONT font, COLORREF color, UINT flags)
{
    SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rc, flags | DT_NOPREFIX);
}

void PaintMainWindow(HWND window, HDC target)
{
    RECT client{};
    GetClientRect(window, &client);
    const int width = client.right;
    const int height = client.bottom;
    const Palette& p = *palette;

    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
    HGDIOBJ previous_bitmap = SelectObject(dc, bitmap);
    HBRUSH background = CreateSolidBrush(p.window);
    FillRect(dc, &client, background);
    DeleteObject(background);

    const int radius = S(12);
    const bool has_selection = !selected_input_paths.empty() || !input_path.empty();
    {
        Gdiplus::Graphics graphics(dc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        if (has_selection)
        {
            FillRounded(graphics, drop_rc, radius, p.card);
            StrokeRounded(graphics, drop_rc, radius, p.accent, 1.5f);
        }
        else
        {
            FillRounded(graphics, drop_rc, radius, p.drop_fill);
            StrokeRounded(graphics, drop_rc, radius, Blend(p.accent, p.card, 45), 1.5f, true);
        }

        const Gdiplus::RectF icon(static_cast<float>(drop_icon_rc.left), static_cast<float>(drop_icon_rc.top),
            static_cast<float>(drop_icon_rc.right - drop_icon_rc.left),
            static_cast<float>(drop_icon_rc.bottom - drop_icon_rc.top));
        Gdiplus::SolidBrush halo(GdiColor(p.accent, 36));
        graphics.FillEllipse(&halo, icon);
        Gdiplus::Pen ring(GdiColor(p.accent), static_cast<float>((std::max)(2, S(2))));
        graphics.DrawEllipse(&ring, Gdiplus::RectF(icon.X + icon.Width * 0.16f, icon.Y + icon.Height * 0.16f,
            icon.Width * 0.68f, icon.Height * 0.68f));
        const float hole = icon.Width * 0.2f;
        Gdiplus::SolidBrush hole_brush(GdiColor(p.accent));
        graphics.FillEllipse(&hole_brush, Gdiplus::RectF(icon.X + (icon.Width - hole) / 2,
            icon.Y + (icon.Height - hole) / 2, hole, hole));

        for (const Card& card : cards)
        {
            FillRounded(graphics, card.rc, radius, p.card);
            StrokeRounded(graphics, card.rc, radius, p.card_border, 1.0f);
        }
        FillRounded(graphics, footer_rc, radius, p.card);
        StrokeRounded(graphics, footer_rc, radius, p.card_border, 1.0f);
    }

    const UINT single_line = DT_SINGLELINE | DT_VCENTER;
    DrawLabel(dc, L"emucompress", header_title_rc, font_title, p.text, single_line | DT_END_ELLIPSIS);
    DrawLabel(dc, L"Convert and extract Xbox, Xbox 360 ZAR, CHD and GameCube/Wii disc images",
        header_subtitle_rc, font_body, p.text_secondary, single_line | DT_END_ELLIPSIS);

    std::wstring drop_title;
    std::wstring drop_detail;
    SelectionText(drop_title, drop_detail);
    DrawLabel(dc, drop_title, drop_title_rc, font_card, p.text, single_line | DT_END_ELLIPSIS);
    DrawLabel(dc, drop_detail, drop_detail_rc, font_small, p.text_secondary, single_line | DT_END_ELLIPSIS);

    for (const Card& card : cards)
    {
        DrawLabel(dc, card.title, card.title_rc, font_card, p.text, single_line);
        DrawLabel(dc, card.caption, card.caption_rc, font_small, p.text_secondary, single_line);
        const std::wstring folder_text = card.folder != nullptr && !card.folder->empty()
            ? L"Output: " + *card.folder
            : std::wstring(L"Output: next to the input file");
        DrawLabel(dc, folder_text, card.folder_rc, font_small, p.text_secondary,
            single_line | DT_RIGHT | DT_PATH_ELLIPSIS);
    }

    BitBlt(target, 0, 0, width, height, dc, 0, 0, SRCCOPY);
    SelectObject(dc, previous_bitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

void PlaceControl(HWND control, int x, int y, int width, int height)
{
    SetWindowPos(control, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

int TextWidth(HDC dc, HFONT font, const std::wstring& text)
{
    SelectObject(dc, font);
    SIZE size{};
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
    return size.cx;
}

int ButtonWidth(HDC dc, HWND button, int padding)
{
    wchar_t label[128]{};
    GetWindowTextW(button, label, ARRAYSIZE(label));
    return TextWidth(dc, font_button, label) + padding * 2;
}

void LayoutUi(HWND window)
{
    RECT client{};
    GetClientRect(window, &client);
    if (client.right <= 0 || cards.empty())
        return;

    HDC dc = GetDC(window);
    const int margin = S(24);
    const int gap = S(16);
    const int pad = S(18);
    const int x0 = margin;
    const int x1 = (std::max)(static_cast<int>(client.right) - margin, x0 + S(320));
    int y = margin;

    HWND about = GetDlgItem(window, static_cast<int>(AboutMenuItemId));
    HWND readme = GetDlgItem(window, static_cast<int>(ReadmeMenuItemId));
    const int about_width = ButtonWidth(dc, about, S(14));
    const int readme_width = ButtonWidth(dc, readme, S(14));
    PlaceControl(about, x1 - about_width, y + S(10), about_width, S(32));
    PlaceControl(readme, x1 - about_width - S(6) - readme_width, y + S(10), readme_width, S(32));
    const int header_right = x1 - about_width - readme_width - S(24);
    header_title_rc = {x0, y, header_right, y + S(32)};
    header_subtitle_rc = {x0, y + S(32), header_right, y + S(54)};
    y += S(54) + S(20);

    const int drop_height = S(92);
    drop_rc = {x0, y, x1, y + drop_height};
    HWND browse = GetDlgItem(window, static_cast<int>(BrowseFilesButtonId));
    const int browse_width = (std::max)(S(150), ButtonWidth(dc, browse, S(22)));
    const int browse_height = S(40);
    PlaceControl(browse, x1 - S(24) - browse_width, y + (drop_height - browse_height) / 2, browse_width, browse_height);
    const int icon_size = S(44);
    drop_icon_rc = {x0 + S(24), y + (drop_height - icon_size) / 2, x0 + S(24) + icon_size,
        y + (drop_height - icon_size) / 2 + icon_size};
    const int text_left = drop_icon_rc.right + S(18);
    const int text_right = (std::max)(text_left, x1 - S(24) - browse_width - S(18));
    const int block_top = y + (drop_height - S(50)) / 2;
    drop_title_rc = {text_left, block_top, text_right, block_top + S(26)};
    drop_detail_rc = {text_left, block_top + S(26), text_right, block_top + S(48)};
    y += drop_height + gap;

    const int row_height = S(30);
    const int button_height = S(36);
    const int button_gap = S(10);
    for (Card& card : cards)
    {
        const int row_top = y + pad;
        const int left = x0 + pad;
        const int right = x1 - pad;

        card.title_rc = {left, row_top, left + TextWidth(dc, font_card, card.title) + S(2), row_top + row_height};
        card.caption_rc = {card.title_rc.right + S(12), row_top,
            card.title_rc.right + S(12) + TextWidth(dc, font_small, card.caption) + S(2), row_top + row_height};

        HWND reset = GetDlgItem(window, static_cast<int>(card.reset_id));
        HWND change = GetDlgItem(window, static_cast<int>(card.change_id));
        const int reset_width = ButtonWidth(dc, reset, S(14));
        const int change_width = ButtonWidth(dc, change, S(14));
        PlaceControl(reset, right - reset_width, row_top, reset_width, row_height);
        PlaceControl(change, right - reset_width - S(6) - change_width, row_top, change_width, row_height);
        const int folder_left = card.caption_rc.right + S(16);
        card.folder_rc = {folder_left, row_top, (std::max)(folder_left, right - reset_width - change_width - S(18)),
            row_top + row_height};

        int button_x = left;
        int button_y = row_top + row_height + S(12);
        int rows = 1;
        for (const OperationSpec& operation : card.operations)
        {
            HWND button = GetDlgItem(window, static_cast<int>(operation.id));
            const int button_width = ButtonWidth(dc, button, S(18));
            if (button_x + button_width > right && button_x > left)
            {
                button_x = left;
                button_y += button_height + button_gap;
                ++rows;
            }
            PlaceControl(button, button_x, button_y, button_width, button_height);
            button_x += button_width + button_gap;
        }

        card.rc = {x0, y, x1, row_top + row_height + S(12) + rows * button_height + (rows - 1) * button_gap + pad};
        y = card.rc.bottom + gap;
    }

    const int footer_height = S(96);
    const int footer_top = (std::max)(y, static_cast<int>(client.bottom) - margin - footer_height);
    footer_rc = {x0, footer_top, x1, footer_top + footer_height};
    const int status_height = S(40);
    HWND cancel = GetDlgItem(window, static_cast<int>(CancelOperationButtonId));
    const int cancel_width = (std::max)(S(140), ButtonWidth(dc, cancel, S(20)));
    PlaceControl(cancel, x1 - pad - cancel_width, footer_top + pad + (status_height - button_height) / 2,
        cancel_width, button_height);
    PlaceControl(status_control, x0 + pad, footer_top + pad,
        (std::max)(S(100), x1 - pad - cancel_width - S(16) - (x0 + pad)), status_height);
    PlaceControl(progress_control, x0 + pad, footer_top + pad + status_height + S(12),
        (x1 - pad) - (x0 + pad), S(8));

    min_content_height = y + footer_height + margin;
    ReleaseDC(window, dc);
    InvalidateRect(window, nullptr, FALSE);
}

void CreateUi(HWND window)
{
    main_window = window;
    current_dpi = GetDpiForWindow(window);
    RebuildFonts();
    ApplyTheme(window);
    CreateTooltipControl(window);

    auto add_card = [](const wchar_t* title, const wchar_t* caption, std::vector<OperationSpec> operations,
                        UINT_PTR change_id, UINT_PTR reset_id, const std::wstring* folder)
    {
        Card card;
        card.title = title;
        card.caption = caption;
        card.operations = std::move(operations);
        card.change_id = change_id;
        card.reset_id = reset_id;
        card.folder = folder;
        cards.push_back(std::move(card));
    };
    add_card(L"CD / DVD", L"CHDMAN \u00B7 CHD archives",
        {{CreateCdButtonId, L"Create CD CHD"}, {CreateDvdButtonId, L"Create DVD CHD"},
            {ExtractCdButtonId, L"Extract CD"}, {ExtractDvdButtonId, L"Extract DVD"},
            {ExtractCdToIsoButtonId, L"Extract CD to ISO"}},
        PickChdmanFolderButtonId, ResetChdmanFolderButtonId, &chdman_output_directory);
    add_card(L"GameCube / Wii", L"Dolphin \u00B7 ISO, GCM, RVZ, WIA and GCZ",
        {{IsoToRvzButtonId, L"ISO to RVZ"}, {RvzToIsoButtonId, L"RVZ to ISO"},
            {IsoToWiaButtonId, L"ISO to WIA"}, {WiaToIsoButtonId, L"WIA to ISO"},
            {IsoToGczButtonId, L"ISO to GCZ"}, {GczToIsoButtonId, L"GCZ to ISO"}},
        PickDolphinFolderButtonId, ResetDolphinFolderButtonId, &dolphin_output_directory);
    add_card(L"Xbox", L"XDVDFS \u00B7 ISO, XISO and ZAR",
        {{PackButtonId, L"ISO to XISO"}, {XisoToIsoButtonId, L"XISO to ISO"}, {UnpackButtonId, L"XISO to folder"},
            {IsoToZarButtonId, L"ISO to ZAR"}, {ZarToXisoButtonId, L"ZAR to XISO"}},
        PickXdvdfsFolderButtonId, ResetXdvdfsFolderButtonId, &xdvdfs_output_directory);

    CreateUiButton(window, BrowseFilesButtonId, L"Choose files...", ButtonKind::Primary, true);
    for (const Card& card : cards)
    {
        for (const OperationSpec& operation : card.operations)
        {
            HWND button = CreateUiButton(window, operation.id, operation.label, ButtonKind::Secondary, true);
            if (operation.id == PackButtonId) pack_button = button;
            else if (operation.id == UnpackButtonId) unpack_button = button;
            else if (operation.id == CreateCdButtonId) create_cd_button = button;
            else if (operation.id == CreateDvdButtonId) create_dvd_button = button;
            else if (operation.id == ExtractCdButtonId) extract_cd_button = button;
            else if (operation.id == ExtractDvdButtonId) extract_dvd_button = button;
            else if (operation.id == ExtractCdToIsoButtonId) extract_cd_to_iso_button = button;
            else if (operation.id == IsoToRvzButtonId) iso_to_rvz_button = button;
            else if (operation.id == RvzToIsoButtonId) rvz_to_iso_button = button;
        }
        HWND change = CreateUiButton(window, card.change_id, L"Change...", ButtonKind::Secondary, true);
        CreateUiButton(window, card.reset_id, L"Reset", ButtonKind::Ghost, true);
        if (card.change_id == PickDolphinFolderButtonId)
            pick_dolphin_folder_button = change;
    }

    status_control = CreateWindowExW(
        0, L"STATIC", L"Choose files or drag a supported file here",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
        0, 0, 10, 10, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(StatusControlId)), nullptr, nullptr);
    SendMessageW(status_control, WM_SETFONT, reinterpret_cast<WPARAM>(font_body), TRUE);
    last_status_text = L"Choose files or drag a supported file here";
    progress_control = CreateWindowExW(
        0, ProgressClassName, nullptr, WS_CHILD | WS_VISIBLE,
        0, 0, 10, 10, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ProgressControlId)), nullptr, nullptr);
    SendMessageW(progress_control, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

    cancel_operation_button = CreateUiButton(
        window, CancelOperationButtonId, L"Cancel operation", ButtonKind::Danger, true);
    SetCancelButtonEnabled(false);
    CreateUiButton(window, ReadmeMenuItemId, L"README", ButtonKind::Ghost, false);
    CreateUiButton(window, AboutMenuItemId, L"About", ButtonKind::Ghost, false);

    UpdateControlStates(window);
    DragAcceptFiles(window, TRUE);
    LayoutUi(window);
}

void FitWindowToContent()
{
    RECT frame{0, 0, S(940), min_content_height};
    AdjustWindowRectExForDpi(&frame, static_cast<DWORD>(GetWindowLongW(main_window, GWL_STYLE)), FALSE,
        static_cast<DWORD>(GetWindowLongW(main_window, GWL_EXSTYLE)), current_dpi);
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(main_window, MONITOR_DEFAULTTONEAREST), &monitor);
    const int work_width = monitor.rcWork.right - monitor.rcWork.left;
    const int work_height = monitor.rcWork.bottom - monitor.rcWork.top;
    const int width = (std::min)(static_cast<int>(frame.right - frame.left), work_width);
    const int height = (std::min)(static_cast<int>(frame.bottom - frame.top), work_height);
    SetWindowPos(main_window, nullptr, monitor.rcWork.left + (work_width - width) / 2,
        monitor.rcWork.top + (work_height - height) / 2, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case WM_CREATE:
        CreateUi(window);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        PaintMainWindow(window, dc);
        EndPaint(window, &paint);
        return 0;
    }

    case WM_DRAWITEM:
    {
        const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
        if (item->CtlType == ODT_BUTTON)
        {
            DrawUiButton(*item);
            return TRUE;
        }
        break;
    }

    case WM_CTLCOLORSTATIC:
        if (reinterpret_cast<HWND>(lparam) == status_control)
        {
            HDC dc = reinterpret_cast<HDC>(wparam);
            SetTextColor(dc, status_tone == StatusTone::Error ? palette->danger
                : (status_tone == StatusTone::Success ? palette->success : palette->text));
            SetBkColor(dc, palette->card);
            return reinterpret_cast<LRESULT>(card_brush);
        }
        break;

    case WM_SETTINGCHANGE:
        if (lparam != 0 && wcscmp(reinterpret_cast<const wchar_t*>(lparam), L"ImmersiveColorSet") == 0)
            ApplyTheme(window);
        return 0;

    case WM_DPICHANGED:
    {
        current_dpi = HIWORD(wparam);
        RebuildFonts();
        if (tooltip_control != nullptr)
            SendMessageW(tooltip_control, TTM_SETMAXTIPWIDTH, 0, S(320));
        const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(window, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
            suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        LayoutUi(window);
        return 0;
    }

    case WM_GETMINMAXINFO:
    {
        auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
        RECT frame{0, 0, S(860), min_content_height > 0 ? min_content_height : S(600)};
        AdjustWindowRectExForDpi(&frame, static_cast<DWORD>(GetWindowLongW(window, GWL_STYLE)), FALSE,
            static_cast<DWORD>(GetWindowLongW(window, GWL_EXSTYLE)), current_dpi);
        info->ptMinTrackSize.x = frame.right - frame.left;
        info->ptMinTrackSize.y = frame.bottom - frame.top;
        return 0;
    }

    case WM_DROPFILES:
        HandleDrop(reinterpret_cast<HDROP>(wparam));
        return 0;

    case WM_COMMAND:
        if (LOWORD(wparam) == AboutMenuItemId)
        {
            MessageBoxW(window,
                L"emucompress is a utility for converting and extracting disc images.\n\n"
                L"It supports Xbox XDVDFS/XISO images, Xbox 360 ISO to ZAR (ZArchive) conversion, CHD creation and extraction, and "
                L"GameCube/Wii ISO, RVZ, WIA and GCZ conversion. Drop a supported file onto the window "
                L"and choose an operation. Output folders can be selected for each format.\n\n"
                L"ZAR conversion is reversible, but the restored image is not byte-identical to the original ISO. "
                L"WIA and GCZ are designed to restore the original ISO exactly, but this is not guaranteed for scrubbed or NKit images.\n\n"
                L"Copyright (C) 2026 chrisis123 and contributors. Licensed under the GNU GPL, "
                L"version 2 or later, with NO WARRANTY. Uses MAME chdman (BSD-3-Clause), xdvdfs (MIT), "
                L"ZArchive (MIT-0), zstd (BSD-3-Clause) and Dolphin (GPL-2.0-or-later). Full license texts and third-party notices are in "
                L"LICENSE, THIRD_PARTY_NOTICES.md and the LICENSES folder next to this program.",
                L"About emucompress", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        if (LOWORD(wparam) == ReadmeMenuItemId)
        {
            wchar_t executable_path[MAX_PATH]{};
            const DWORD length = GetModuleFileNameW(nullptr, executable_path, ARRAYSIZE(executable_path));
            if (length == 0 || length >= ARRAYSIZE(executable_path))
            {
                MessageBoxW(window, L"Unable to locate the application folder.", L"emucompress",
                    MB_OK | MB_ICONERROR);
                return 0;
            }

            std::wstring readme_path(executable_path, length);
            const size_t separator = readme_path.find_last_of(L"\\/");
            if (separator == std::wstring::npos)
            {
                MessageBoxW(window, L"Unable to locate the application folder.", L"emucompress",
                    MB_OK | MB_ICONERROR);
                return 0;
            }
            readme_path.resize(separator + 1);
            readme_path += L"README.md";

            if (GetFileAttributesW(readme_path.c_str()) == INVALID_FILE_ATTRIBUTES)
            {
                MessageBoxW(window,
                    L"README.md was not found beside emucompress_gui.exe. Rebuild or copy the README into the application folder.",
                    L"emucompress help", MB_OK | MB_ICONWARNING);
                return 0;
            }

            const HINSTANCE result = ShellExecuteW(window, L"open", readme_path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            if (reinterpret_cast<INT_PTR>(result) <= 32)
            {
                MessageBoxW(window, L"Windows could not open README.md.", L"emucompress help",
                    MB_OK | MB_ICONERROR);
            }
            return 0;
        }
        if (HIWORD(wparam) == BN_CLICKED && LOWORD(wparam) == CancelOperationButtonId)
        {
            RequestCancelOperation(window);
            return 0;
        }
        if (HIWORD(wparam) == BN_CLICKED)
        {
            if (LOWORD(wparam) == BrowseFilesButtonId)
            {
                std::vector<std::wstring> files = PickInputFiles();
                if (!files.empty())
                {
                    selected_input_paths = std::move(files);
                    input_path = selected_input_paths.front();
                    RefreshSelection();
                    SetStatus(std::to_wstring(selected_input_paths.size()) +
                        L" file(s) selected. Choose an operation.");
                }
                return 0;
            }

            if (LOWORD(wparam) == ResetXdvdfsFolderButtonId || LOWORD(wparam) == ResetChdmanFolderButtonId ||
                LOWORD(wparam) == ResetDolphinFolderButtonId)
            {
                std::wstring& directory = LOWORD(wparam) == ResetXdvdfsFolderButtonId ? xdvdfs_output_directory
                    : (LOWORD(wparam) == ResetChdmanFolderButtonId ? chdman_output_directory
                                                                    : dolphin_output_directory);
                directory.clear();
                UpdateFolderUi();
                SetStatus(L"Output folder reset: results are written next to the input file.");
                return 0;
            }

            if (LOWORD(wparam) == PickDolphinFolderButtonId)
            {
                const std::wstring selected = PickOutputDirectory(dolphin_output_directory);
                if (!selected.empty())
                {
                    dolphin_output_directory = selected;
                    UpdateFolderUi();
                    SetStatus(L"GameCube/Wii output folder: " + selected);
                }
                return 0;
            }
            if (LOWORD(wparam) == PickXdvdfsFolderButtonId)
            {
                const std::wstring selected = PickOutputDirectory(xdvdfs_output_directory);
                if (!selected.empty())
                {
                    xdvdfs_output_directory = selected;
                    UpdateFolderUi();
                    SetStatus(L"XDVDFS output folder: " + selected);
                }
                return 0;
            }
            if (LOWORD(wparam) == PickChdmanFolderButtonId)
            {
                const std::wstring selected = PickOutputDirectory(chdman_output_directory);
                if (!selected.empty())
                {
                    chdman_output_directory = selected;
                    UpdateFolderUi();
                    SetStatus(L"CHDMAN output folder: " + selected);
                }
                return 0;
            }
            const UINT_PTR operation_id = LOWORD(wparam);
            if (operation_id == PackButtonId || operation_id == UnpackButtonId ||
                operation_id == XisoToIsoButtonId || operation_id == CreateCdButtonId ||
                operation_id == CreateDvdButtonId || operation_id == ExtractCdButtonId ||
                operation_id == ExtractDvdButtonId || operation_id == ExtractCdToIsoButtonId ||
                operation_id == IsoToRvzButtonId || operation_id == RvzToIsoButtonId ||
                operation_id == IsoToWiaButtonId || operation_id == WiaToIsoButtonId ||
                operation_id == IsoToGczButtonId || operation_id == GczToIsoButtonId ||
                operation_id == IsoToZarButtonId || operation_id == ZarToXisoButtonId)
            {
                BeginOperation(operation_id);
            }
        }
        return 0;

    case WM_TIMER:
        if (wparam == ConversionTimerId && conversion_active.load())
        {
            UpdateProgress(conversion_unpack.load());
        }
        return 0;

    case ConversionCompleteMessage:
    {
        KillTimer(window, ConversionTimerId);
        if (conversion_thread.joinable())
        {
            conversion_thread.join();
        }

        int status;
        std::string error;
        {
            std::lock_guard<std::mutex> lock(result_mutex);
            status = conversion_result;
            error = conversion_error;
        }
        if (status == XDVDFS_SUCCESS)
        {
            if (batch_running)
                ++batch_success_count;
            SendMessageW(progress_control, PBM_SETPOS, 100, 0);
            std::wstring completion = cancel_current_operation
                ? L"Current operation finished after cancellation was requested."
                : L"Completed: " + conversion_output;
            const int kind = conversion_kind.load();
            if (!cancel_current_operation && (kind == ConversionKindIsoToZar || kind == ConversionKindZarToXiso))
            {
                completion += kind == ConversionKindIsoToZar
                    ? L" - reversible via ZAR to XISO, but not byte-identical to the original ISO."
                    : L" - all files restored; not byte-identical to the original ISO.";
                if (!conversion_notice.empty())
                    completion += L" " + conversion_notice;
            }
            if (!cancel_current_operation && kind == 5)
            {
                const int format = dolphin_target_format.load();
                const unsigned int notes = dolphin_notes.load();
                if (format == DolphinFormatWia || format == DolphinFormatGcz)
                {
                    completion += L" - converting it back to ISO restores the original image";
                    completion += (notes & DolphinNoteNkit) != 0
                        ? L", but this source is an NKit image, so the original disc cannot be fully restored."
                        : L" (scrubbed or trimmed sources are restored as they were stored).";
                    if ((notes & DolphinNoteWiiGcz) != 0)
                        completion += L" Note: GCZ saves very little space on Wii discs; RVZ compresses better.";
                }
                else if ((notes & DolphinNoteNkit) != 0)
                {
                    completion += L" - note: this is an NKit image, so the output is still NKit and not the original disc.";
                }
            }
            SetStatus(completion);
        }
        else
        {
            SetStatus(status == XDVDFS_CANCELLED || (cancel_current_operation && conversion_kind.load() == 5)
                ? L"Current conversion canceled."
                : L"Conversion failed (" + std::to_wstring(status) + L"): " + ToWide(error.c_str()));
        }
        SendMessageW(progress_control, PBM_SETMARQUEE, FALSE, 0);
        if (batch_running)
        {
            ++batch_input_index;
            StartNextBatchItem();
        }
        if (!batch_running)
        {
            cancel_requested.store(false);
            cancel_current_operation = false;
            batch_stop_requested = false;
            SetCancelButtonEnabled(false);
            SetOperationsEnabled(false);
        }
        return 0;
    }

    case DolphinProgressMessage:
        if (conversion_active.load() && conversion_kind.load() == 5)
            SendMessageW(progress_control, PBM_SETPOS, dolphin_progress_percent.load(), 0);
        return 0;

    case WM_SIZE:
        if (status_control != nullptr)
            LayoutUi(window);
        return 0;

    case WM_DESTROY:
        if (conversion_thread.joinable())
        {
            conversion_thread.join();
        }
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(window, message, wparam, lparam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command)
{
    INITCOMMONCONTROLSEX common_controls{sizeof(common_controls), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES};
    InitCommonControlsEx(&common_controls);

    Gdiplus::GdiplusStartupInput gdiplus_input;
    ULONG_PTR gdiplus_token = 0;
    if (Gdiplus::GdiplusStartup(&gdiplus_token, &gdiplus_input, nullptr) != Gdiplus::Ok)
    {
        return 1;
    }

    WNDCLASSEXW progress_class{};
    progress_class.cbSize = sizeof(progress_class);
    progress_class.hInstance = instance;
    progress_class.lpfnWndProc = ProgressWindowProc;
    progress_class.lpszClassName = ProgressClassName;
    progress_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.hInstance = instance;
    window_class.lpfnWndProc = WindowProc;
    window_class.lpszClassName = WindowClassName;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(ApplicationIconResourceId));
    window_class.hIconSm = window_class.hIcon;

    if (!RegisterClassExW(&progress_class) || !RegisterClassExW(&window_class))
    {
        Gdiplus::GdiplusShutdown(gdiplus_token);
        return 1;
    }

    const UINT system_dpi = GetDpiForSystem();
    main_window = CreateWindowExW(
        0, WindowClassName, L"emucompress",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        MulDiv(940, static_cast<int>(system_dpi), 96), MulDiv(720, static_cast<int>(system_dpi), 96),
        nullptr, nullptr, instance, nullptr);
    if (main_window == nullptr)
    {
        Gdiplus::GdiplusShutdown(gdiplus_token);
        return 1;
    }

    FitWindowToContent();
    ShowWindow(main_window, show_command);
    UpdateWindow(main_window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        if (!IsDialogMessageW(main_window, &message))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    Gdiplus::GdiplusShutdown(gdiplus_token);
    return static_cast<int>(message.wParam);
}
