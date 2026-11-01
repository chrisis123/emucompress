#include <windows.h>
#include <commctrl.h>
#include <imapi2fs.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "xdvdfs_ffi.h"

namespace
{
constexpr wchar_t WindowClassName[] = L"XdvdfsDragDropWindow";
constexpr UINT ConversionTimerId = 1;
constexpr UINT ConversionCompleteMessage = WM_APP + 1;
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
std::thread conversion_thread;
std::atomic<bool> conversion_active = false;
std::atomic<bool> conversion_unpack = false;
std::atomic<int> conversion_kind = 0;
std::atomic<unsigned long long> total_bytes = 1;
std::atomic<unsigned long long> completed_bytes = 0;
std::chrono::steady_clock::time_point conversion_start;
std::mutex result_mutex;
int conversion_result = XDVDFS_OPERATION_FAILED;
std::string conversion_error;
std::wstring conversion_output;
std::wstring conversion_operation;
std::wstring input_path;

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

std::wstring ChdmanPath()
{
    wchar_t module_path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, module_path, ARRAYSIZE(module_path));
    if (length == 0 || length >= ARRAYSIZE(module_path))
    {
        return L"chdman.exe";
    }

    std::wstring path(module_path, length);
    const size_t separator = path.find_last_of(L"\\/");
    if (separator != std::wstring::npos)
    {
        path.resize(separator + 1);
        path += L"..\\..\\..\\..\\chdman.exe";
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            return path;
        }
    }

    return L"chdman.exe";
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

std::wstring TemporaryDirectory()
{
    wchar_t temporary_path[MAX_PATH]{};
    const DWORD length = GetTempPathW(ARRAYSIZE(temporary_path), temporary_path);
    if (length == 0 || length >= ARRAYSIZE(temporary_path))
    {
        return {};
    }

    wchar_t temporary_file[MAX_PATH]{};
    if (GetTempFileNameW(temporary_path, L"xiso", 0, temporary_file) == 0 ||
        !DeleteFileW(temporary_file) || !CreateDirectoryW(temporary_file, nullptr))
    {
        return {};
    }
    return temporary_file;
}

void RemoveTemporaryDirectory(const std::wstring& path)
{
    if (path.empty())
    {
        return;
    }

    std::wstring source = path;
    source.push_back(L'\0');
    source.push_back(L'\0');
    SHFILEOPSTRUCTW operation{};
    operation.wFunc = FO_DELETE;
    operation.pFrom = source.c_str();
    operation.fFlags = FOF_NO_UI | FOF_NOCONFIRMATION | FOF_SILENT;
    SHFileOperationW(&operation);
}

std::string HResultError(HRESULT result)
{
    return "Windows operation failed with HRESULT 0x" +
        std::to_string(static_cast<unsigned long>(result));
}

HRESULT CreateStandardIso(const std::wstring& source, const std::wstring& output)
{
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitialize = SUCCEEDED(result);
    if (FAILED(result) && result != RPC_E_CHANGED_MODE)
    {
        return result;
    }

    IFileSystemImage* image = nullptr;
    IFileSystemImageResult* image_result = nullptr;
    HRESULT operation = CoCreateInstance(CLSID_MsftFileSystemImage, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&image));
    if (SUCCEEDED(operation)) operation = image->put_FileSystemsToCreate(FsiFileSystemISO9660);
    IFsiDirectoryItem* root = nullptr;
    if (SUCCEEDED(operation)) operation = image->get_Root(&root);
    BSTR source_bstr = SysAllocString(source.c_str());
    if (SUCCEEDED(operation)) operation = root->AddTree(source_bstr, VARIANT_TRUE);
    SysFreeString(source_bstr);
    if (root) root->Release();
    if (SUCCEEDED(operation)) operation = image->CreateResultImage(&image_result);
    IStream* stream = nullptr;
    if (SUCCEEDED(operation)) operation = image_result->get_ImageStream(&stream);
    HANDLE file = INVALID_HANDLE_VALUE;
    if (SUCCEEDED(operation)) file = CreateFileW(output.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (SUCCEEDED(operation) && file == INVALID_HANDLE_VALUE) operation = HRESULT_FROM_WIN32(GetLastError());
    if (SUCCEEDED(operation))
    {
        LARGE_INTEGER zero{};
        ULARGE_INTEGER position{};
        operation = stream->Seek(zero, STREAM_SEEK_SET, &position);
        unsigned char buffer[1024 * 1024];
        while (SUCCEEDED(operation))
        {
            ULONG read = 0;
            operation = stream->Read(buffer, sizeof(buffer), &read);
            if (FAILED(operation) || read == 0) break;
            DWORD written = 0;
            if (!WriteFile(file, buffer, read, &written, nullptr) || written != read)
            {
                operation = HRESULT_FROM_WIN32(GetLastError());
                break;
            }
        }
        if (operation == S_FALSE) operation = S_OK;
    }
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (stream) stream->Release();
    if (image_result) image_result->Release();
    if (image) image->Release();
    if (uninitialize) CoUninitialize();
    return operation;
}

void StartConversion(const std::wstring& input, bool to_standard_iso = false)
{
    if (conversion_active.exchange(true))
    {
        SetStatus(L"A conversion is already running.");
        return;
    }

    const bool unpack = EndsWith(input, L".xiso.iso");
    if (conversion_thread.joinable())
    {
        conversion_thread.join();
    }

    const std::wstring output = to_standard_iso
        ? StandardIsoPathForXiso(input)
        : OutputPathFor(input, unpack);
    const auto total = FileSize(input);
    total_bytes.store(total == 0 ? 1 : total);
    conversion_unpack.store(false);
    conversion_kind.store(to_standard_iso ? 4 : (unpack ? 2 : 1));
    completed_bytes.store(0);
    conversion_output = output;
    conversion_start = std::chrono::steady_clock::now();
    SendMessageW(progress_control, PBM_SETPOS, 0, 0);
    SetStatus(to_standard_iso
        ? L"Converting XISO to ISO... ETA: calculating..."
        : (unpack ? L"Extracting XISO... ETA: calculating..." : L"Creating XISO... ETA: calculating..."));
    SetTimer(main_window, ConversionTimerId, 250, nullptr);

    conversion_thread = std::thread([input, output, unpack, to_standard_iso]()
    {
        std::vector<unsigned char> error(4096, 0);
        int status = XDVDFS_OPERATION_FAILED;
        std::string error_message;
        if (to_standard_iso)
        {
            const std::wstring temporary = TemporaryDirectory();
            if (temporary.empty())
            {
                error_message = "Unable to create a temporary extraction directory";
            }
            else
            {
                const std::string input_utf8 = ToUtf8(input);
                const std::string temporary_utf8 = ToUtf8(temporary);
                status = xdvdfs_unpack(input_utf8.c_str(), temporary_utf8.c_str(), error.data(), error.size());
                if (status == XDVDFS_SUCCESS)
                {
                    const HRESULT result = CreateStandardIso(temporary, output);
                    status = SUCCEEDED(result) ? XDVDFS_SUCCESS : XDVDFS_OPERATION_FAILED;
                    if (FAILED(result))
                    {
                        error_message = HResultError(result);
                    }
                }
                RemoveTemporaryDirectory(temporary);
            }
        }
        else
        {
            const std::string input_utf8 = ToUtf8(input);
            const std::string output_utf8 = ToUtf8(output);
            status = unpack
                ? xdvdfs_unpack(input_utf8.c_str(), output_utf8.c_str(), error.data(), error.size())
                : xdvdfs_pack(input_utf8.c_str(), output_utf8.c_str(), error.data(), error.size());
        }

        if (error_message.empty())
        {
            error_message.assign(reinterpret_cast<const char*>(error.data()));
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

    const std::wstring output = ReplaceExtension(input, extension);
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

    conversion_thread = std::thread([input, output, command_output, command, extract_cd_to_iso]()
    {
        std::wstring command_line = L"\"" + ChdmanPath() + L"\" " + command +
            L" -i \"" + input + L"\" -o \"" + command_output + L"\"";
        if (extract_cd_to_iso)
        {
            command_line += L" -ob \"" + output + L"\"";
        }
        std::vector<wchar_t> command_buffer(command_line.begin(), command_line.end());
        command_buffer.push_back(L'\0');

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        int status = XDVDFS_OPERATION_FAILED;
        std::string error = "Unable to start chdman.exe";
        if (CreateProcessW(nullptr, command_buffer.data(), nullptr, nullptr, FALSE,
                CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
        {
            WaitForSingleObject(process.hProcess, INFINITE);
            DWORD exit_code = 1;
            GetExitCodeProcess(process.hProcess, &exit_code);
            status = exit_code == 0 ? XDVDFS_SUCCESS : XDVDFS_OPERATION_FAILED;
            error = exit_code == 0 ? "" : "chdman.exe exited with code " + std::to_string(exit_code);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);

            if (exit_code == 0 && extract_cd_to_iso)
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
        SetStatus(L"Selected: " + input_path + L". Choose an operation.");
    }
    DragFinish(drop);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case WM_CREATE:
        status_control = CreateWindowExW(
            0, L"STATIC", L"Drag a supported file here",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            20, 35, 560, 35, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(StatusControlId)), nullptr, nullptr);
        progress_control = CreateWindowExW(
            0, PROGRESS_CLASSW, nullptr,
            WS_CHILD | WS_VISIBLE,
            20, 85, 660, 24, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ProgressControlId)), nullptr, nullptr);
        CreateWindowExW(0, L"STATIC", L"XDVDFS", WS_CHILD | WS_VISIBLE,
            20, 125, 60, 25, window, nullptr, nullptr, nullptr);
        pack_button = CreateWindowExW(
            0, L"BUTTON", L"ISO to XISO", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            85, 120, 105, 28, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(PackButtonId)), nullptr, nullptr);
        CreateWindowExW(
            0, L"BUTTON", L"XISO to ISO", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            195, 120, 100, 28, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(XisoToIsoButtonId)), nullptr, nullptr);
        unpack_button = CreateWindowExW(
            0, L"BUTTON", L"XISO to folder", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            300, 120, 115, 28, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(UnpackButtonId)), nullptr, nullptr);
        CreateWindowExW(0, L"STATIC", L"CHDMAN", WS_CHILD | WS_VISIBLE,
            20, 160, 60, 25, window, nullptr, nullptr, nullptr);
        create_cd_button = CreateWindowExW(
            0, L"BUTTON", L"Create CD CHD", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            85, 155, 110, 28, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(CreateCdButtonId)), nullptr, nullptr);
        create_dvd_button = CreateWindowExW(
            0, L"BUTTON", L"Create DVD CHD", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            200, 155, 110, 28, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(CreateDvdButtonId)), nullptr, nullptr);
        extract_cd_button = CreateWindowExW(
            0, L"BUTTON", L"Extract CD", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            315, 155, 100, 28, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ExtractCdButtonId)), nullptr, nullptr);
        extract_dvd_button = CreateWindowExW(
            0, L"BUTTON", L"Extract DVD", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            420, 155, 105, 28, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ExtractDvdButtonId)), nullptr, nullptr);
        extract_cd_to_iso_button = CreateWindowExW(
            0, L"BUTTON", L"Extract CD to ISO", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            530, 155, 130, 28, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ExtractCdToIsoButtonId)), nullptr, nullptr);
        SendMessageW(progress_control, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        DragAcceptFiles(window, TRUE);
        return 0;

    case WM_DROPFILES:
        HandleDrop(reinterpret_cast<HDROP>(wparam));
        return 0;

    case WM_COMMAND:
        if (HIWORD(wparam) == BN_CLICKED)
        {
            if (input_path.empty())
            {
                SetStatus(L"Drop an input file first.");
                return 0;
            }

            switch (LOWORD(wparam))
            {
            case PackButtonId:
                StartConversion(input_path);
                return 0;
            case UnpackButtonId:
                if (EndsWith(input_path, L".xiso.iso"))
                {
                    StartConversion(input_path);
                }
                else
                {
                    SetStatus(L"XISO to folder requires a .xiso.iso input.");
                }
                return 0;
            case XisoToIsoButtonId:
                if (EndsWith(input_path, L".xiso.iso"))
                {
                    StartConversion(input_path, true);
                }
                else
                {
                    SetStatus(L"XISO to ISO requires a .xiso.iso input.");
                }
                return 0;
            case CreateCdButtonId:
                if (!EndsWith(input_path, L".cue") && !EndsWith(input_path, L".gdi") && !EndsWith(input_path, L".iso"))
                {
                    SetStatus(L"Create CD CHD requires a .cue, .gdi, or .iso input.");
                    return 0;
                }
                StartChdmanConversion(
                    input_path, L"createcd",
                    L"chd", L"Creating CD CHD...");
                return 0;
            case CreateDvdButtonId:
                if (!EndsWith(input_path, L".iso"))
                {
                    SetStatus(L"Create DVD CHD requires an .iso input.");
                    return 0;
                }
                StartChdmanConversion(
                    input_path, L"createdvd",
                    L"chd", L"Creating DVD CHD...");
                return 0;
            case ExtractCdButtonId:
                if (!EndsWith(input_path, L".chd"))
                {
                    SetStatus(L"Extract CD requires a .chd input.");
                    return 0;
                }
                StartChdmanConversion(
                    input_path, L"extractcd",
                    L"cue", L"Extracting CD...");
                return 0;
            case ExtractDvdButtonId:
                if (!EndsWith(input_path, L".chd"))
                {
                    SetStatus(L"Extract DVD requires a .chd input.");
                    return 0;
                }
                StartChdmanConversion(
                    input_path, L"extractdvd",
                    L"iso", L"Extracting DVD...");
                return 0;
            case ExtractCdToIsoButtonId:
                if (!EndsWith(input_path, L".chd"))
                {
                    SetStatus(L"Extract CD to ISO requires a .chd input.");
                    return 0;
                }
                StartChdmanConversion(
                    input_path, L"extractcd",
                    L"iso", L"Extracting CD to ISO...", true);
                return 0;
            default:
                break;
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
            SendMessageW(progress_control, PBM_SETPOS, 100, 0);
            SetStatus(L"Completed: " + conversion_output);
        }
        else
        {
            SetStatus(L"Conversion failed (" + std::to_wstring(status) + L"): " + ToWide(error.c_str()));
        }
        return 0;
    }

    case WM_SIZE:
        if (status_control != nullptr)
        {
            const int width = LOWORD(lparam) - 40;
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
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (!RegisterClassExW(&window_class))
    {
        return 1;
    }

    main_window = CreateWindowExW(
             0, WindowClassName, L"emucompress",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 700, 230,
        nullptr, nullptr, instance, nullptr);
    if (main_window == nullptr)
    {
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
