#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "xdvdfs_ffi.h"
#include "chdman_api.h"

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
std::atomic<unsigned long long> dolphin_progress_bytes = 0;
std::chrono::steady_clock::time_point conversion_start;
std::mutex result_mutex;
int conversion_result = XDVDFS_OPERATION_FAILED;
std::string conversion_error;
std::wstring conversion_output;
std::wstring conversion_operation;
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

void SetStatus(const std::wstring& text);

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

void SetStatus(const std::wstring& text)
{
    SetWindowTextW(status_control, text.c_str());
}

void SetCancelButtonEnabled(bool enabled)
{
    if (cancel_operation_button != nullptr)
        EnableWindow(cancel_operation_button, enabled ? TRUE : FALSE);
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

void StartDolphinConversion(const std::wstring& input, bool to_rvz)
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

    const std::wstring output = OutputPathInDirectory(
        ReplaceExtension(input, to_rvz ? L"rvz" : L"iso"), dolphin_output_directory);
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
    conversion_operation = to_rvz
        ? L"Compressing GameCube/Wii ISO to RVZ..."
        : L"Converting GameCube/Wii RVZ to ISO...";
    conversion_start = std::chrono::steady_clock::now();
    SendMessageW(progress_control, PBM_SETPOS, 0, 0);
    SendMessageW(progress_control, PBM_SETMARQUEE, FALSE, 0);
    SetStatus(to_rvz ? L"Compressing GameCube/Wii ISO to RVZ..." : L"Converting RVZ to ISO...");
    SetTimer(main_window, ConversionTimerId, 250, nullptr);
    SetCancelButtonEnabled(true);

    conversion_thread = std::thread([input, output, to_rvz, output_existed]()
    {
        int status = XDVDFS_OPERATION_FAILED;
        std::string error;
        HMODULE library = LoadLibraryW(DolphinRvzDllPath().c_str());
        if (library != nullptr)
        {
            using ProgressCallback = int (*)(float, void*);
            using ConvertFunction = int (*)(const char*, const char*, int, ProgressCallback, void*, char*, unsigned int);
            const auto convert = reinterpret_cast<ConvertFunction>(GetProcAddress(library, "DolphinRVZConvert"));
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
                const int result = convert(input_utf8.c_str(), output_utf8.c_str(), to_rvz ? 1 : 0,
                    report_progress, &progress_context, diagnostic.data(),
                    static_cast<unsigned int>(diagnostic.size()));
                status = result == 0 ? XDVDFS_SUCCESS : XDVDFS_OPERATION_FAILED;
                if (result != 0)
                {
                    error = diagnostic.data();
                    if (error.empty())
                    {
                        error = cancel_requested.load()
                            ? "Conversion canceled by user"
                            : "Dolphin RVZ conversion failed with code " + std::to_string(result);
                    }
                }
            }
            else
            {
                error = "DolphinRVZ.dll does not export DolphinRVZConvert";
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

void HandleDrop(HDROP drop)
{
    const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    if (count > 0)
    {
        const UINT length = DragQueryFileW(drop, 0, nullptr, 0);
        std::wstring path(static_cast<size_t>(length) + 1, L'\0');
        DragQueryFileW(drop, 0, &path[0], length + 1);
        path.resize(length);
        input_path = path;
        selected_input_paths = {path};
        SetStatus(L"Selected: " + input_path + L". Choose an operation.");
    }
    DragFinish(drop);
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
        if (!EndsWith(input, L".iso"))
        {
            SetStatus(L"ISO to RVZ requires a GameCube/Wii .iso input.");
            return false;
        }
        StartDolphinConversion(input, true);
        break;
    case RvzToIsoButtonId:
        if (!EndsWith(input, L".rvz"))
        {
            SetStatus(L"RVZ to ISO requires a .rvz input.");
            return false;
        }
        StartDolphinConversion(input, false);
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

    const bool rvz_operation = conversion_kind.load() == 5;
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

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case WM_CREATE:
        CreateWindowExW(
            0, L"BUTTON", L"Choose files...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            20, 10, 110, 26, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(BrowseFilesButtonId)), nullptr, nullptr);
        cancel_operation_button = CreateWindowExW(
            0, L"BUTTON", L"Cancel operation", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            140, 10, 130, 26, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(CancelOperationButtonId)), nullptr, nullptr);
        SetCancelButtonEnabled(false);
        status_control = CreateWindowExW(
            0, L"STATIC", L"Choose files or drag a supported file here",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            20, 35, 560, 35, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(StatusControlId)), nullptr, nullptr);
        progress_control = CreateWindowExW(
            0, PROGRESS_CLASSW, nullptr,
            WS_CHILD | WS_VISIBLE | PBS_MARQUEE,
            20, 85, 1060, 24, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ProgressControlId)), nullptr, nullptr);
        CreateWindowExW(0, L"STATIC", L"XDVDFS", WS_CHILD | WS_VISIBLE,
            20, 125, 60, 25, window, nullptr, nullptr, nullptr);
        pack_button = CreateWindowExW(
            0, L"BUTTON", L"ISO to XISO", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            100, 120, 115, 28, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(PackButtonId)), nullptr, nullptr);
        CreateWindowExW(
            0, L"BUTTON", L"XISO to ISO", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            225, 120, 115, 28, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(XisoToIsoButtonId)), nullptr, nullptr);
        unpack_button = CreateWindowExW(
            0, L"BUTTON", L"XISO to folder", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            350, 120, 130, 28, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(UnpackButtonId)), nullptr, nullptr);
        CreateWindowExW(
            0, L"BUTTON", L"XDVDFS output folder...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            490, 120, 190, 28, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(PickXdvdfsFolderButtonId)), nullptr, nullptr);
        CreateWindowExW(0, L"STATIC", L"CHDMAN", WS_CHILD | WS_VISIBLE,
            20, 165, 70, 25, window, nullptr, nullptr, nullptr);
        create_cd_button = CreateWindowExW(
            0, L"BUTTON", L"Create CD CHD", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            100, 160, 130, 28, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(CreateCdButtonId)), nullptr, nullptr);
        create_dvd_button = CreateWindowExW(
            0, L"BUTTON", L"Create DVD CHD", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            240, 160, 135, 28, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(CreateDvdButtonId)), nullptr, nullptr);
        extract_cd_button = CreateWindowExW(
            0, L"BUTTON", L"Extract CD", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            385, 160, 110, 28, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ExtractCdButtonId)), nullptr, nullptr);
        extract_dvd_button = CreateWindowExW(
            0, L"BUTTON", L"Extract DVD", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            505, 160, 120, 28, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ExtractDvdButtonId)), nullptr, nullptr);
        extract_cd_to_iso_button = CreateWindowExW(
            0, L"BUTTON", L"Extract CD to ISO", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            635, 160, 150, 28, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ExtractCdToIsoButtonId)), nullptr, nullptr);
        CreateWindowExW(
            0, L"BUTTON", L"CHDMAN output folder...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            795, 160, 220, 28, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(PickChdmanFolderButtonId)), nullptr, nullptr);
        CreateWindowExW(0, L"STATIC", L"GC/Wii", WS_CHILD | WS_VISIBLE,
            20, 205, 70, 25, window, nullptr, nullptr, nullptr);
        iso_to_rvz_button = CreateWindowExW(
            0, L"BUTTON", L"ISO to RVZ", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            100, 200, 115, 28, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IsoToRvzButtonId)), nullptr, nullptr);
        rvz_to_iso_button = CreateWindowExW(
            0, L"BUTTON", L"RVZ to ISO", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            225, 200, 115, 28, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(RvzToIsoButtonId)), nullptr, nullptr);
        pick_dolphin_folder_button = CreateWindowExW(
            0, L"BUTTON", L"GC/Wii output folder...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            350, 200, 200, 28, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(PickDolphinFolderButtonId)), nullptr, nullptr);
        SendMessageW(progress_control, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        DragAcceptFiles(window, TRUE);
        return 0;

    case WM_DROPFILES:
        HandleDrop(reinterpret_cast<HDROP>(wparam));
        return 0;

    case WM_COMMAND:
        if (LOWORD(wparam) == AboutMenuItemId)
        {
            MessageBoxW(window,
                L"emucompress is a utility for converting and extracting disc images.\n\n"
                L"It supports Xbox XDVDFS/XISO images, CHD creation and extraction, and "
                L"GameCube/Wii ISO and RVZ conversion. Drop a supported file onto the window "
                L"and choose an operation. Output folders can be selected for each format.",
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
                    SetStatus(std::to_wstring(selected_input_paths.size()) +
                        L" file(s) selected. Choose an operation.");
                }
                return 0;
            }

            if (LOWORD(wparam) == PickDolphinFolderButtonId)
            {
                const std::wstring selected = PickOutputDirectory(dolphin_output_directory);
                if (!selected.empty())
                {
                    dolphin_output_directory = selected;
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
                    SetStatus(L"CHDMAN output folder: " + selected);
                }
                return 0;
            }
            const UINT_PTR operation_id = LOWORD(wparam);
            if (operation_id == PackButtonId || operation_id == UnpackButtonId ||
                operation_id == XisoToIsoButtonId || operation_id == CreateCdButtonId ||
                operation_id == CreateDvdButtonId || operation_id == ExtractCdButtonId ||
                operation_id == ExtractDvdButtonId || operation_id == ExtractCdToIsoButtonId ||
                operation_id == IsoToRvzButtonId || operation_id == RvzToIsoButtonId)
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
            SetStatus(cancel_current_operation
                ? L"Current operation finished after cancellation was requested."
                : L"Completed: " + conversion_output);
        }
        else
        {
            SetStatus(cancel_current_operation && conversion_kind.load() == 5
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
        }
        return 0;
    }

    case DolphinProgressMessage:
        if (conversion_active.load() && conversion_kind.load() == 5)
            SendMessageW(progress_control, PBM_SETPOS, dolphin_progress_percent.load(), 0);
        return 0;

    case WM_SIZE:
        if (status_control != nullptr)
        {
            const int width = (std::max)(100, static_cast<int>(LOWORD(lparam)) - 40);
            MoveWindow(status_control, 20, 35, width, 35, TRUE);
            MoveWindow(progress_control, 20, 85, width, 24, TRUE);
        }
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
    INITCOMMONCONTROLSEX common_controls{sizeof(common_controls), ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&common_controls);

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.hInstance = instance;
    window_class.lpfnWndProc = WindowProc;
    window_class.lpszClassName = WindowClassName;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(ApplicationIconResourceId));
    window_class.hIconSm = window_class.hIcon;
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (!RegisterClassExW(&window_class))
    {
        return 1;
    }

    HMENU menu = CreateMenu();
    HMENU help_menu = CreatePopupMenu();
    if (menu == nullptr || help_menu == nullptr ||
        !AppendMenuW(help_menu, MF_STRING, AboutMenuItemId, L"About emucompress") ||
        !AppendMenuW(help_menu, MF_STRING, ReadmeMenuItemId, L"Read README") ||
        !AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(help_menu), L"Help"))
    {
        if (help_menu != nullptr)
            DestroyMenu(help_menu);
        if (menu != nullptr)
            DestroyMenu(menu);
        return 1;
    }

    main_window = CreateWindowExW(
             0, WindowClassName, L"emucompress",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1100, 300,
        nullptr, menu, instance, nullptr);
    if (main_window == nullptr)
    {
        DestroyMenu(menu);
        return 1;
    }

    ShowWindow(main_window, show_command);
    UpdateWindow(main_window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}
