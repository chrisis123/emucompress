#include <windows.h>

#include <iostream>
#include <string>
#include <vector>

#include "xdvdfs_ffi.h"

namespace
{
std::string ToUtf8(const wchar_t* value)
{
	if (value == nullptr || *value == L'\0')
	{
		return {};
	}

	const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, nullptr, 0, nullptr, nullptr);
	if (length <= 0)
	{
		return {};
	}

	std::string result(static_cast<size_t>(length), '\0');
	if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, &result[0], length, nullptr, nullptr) <= 0)
	{
		return {};
	}

	result.resize(static_cast<size_t>(length - 1));
	return result;
}
}

int wmain(int argc, wchar_t* argv[])
{
	if (argc != 3)
	{
		std::wcerr << L"Usage: xdvdfs_win32.exe <source-directory-or-iso> <output-xiso>\n";
		return XDVDFS_INVALID_ARGUMENT;
	}

	const std::string source = ToUtf8(argv[1]);
	const std::string output = ToUtf8(argv[2]);
	if (source.empty() || output.empty())
	{
		std::wcerr << L"Source and output paths must be valid UTF-8.\n";
		return XDVDFS_INVALID_ARGUMENT;
	}

	std::vector<unsigned char> error(4096, 0);
	const int status = xdvdfs_pack(source.c_str(), output.c_str(), error.data(), error.size());
	if (status != XDVDFS_SUCCESS)
	{
		std::cerr << "xdvdfs_pack failed (" << status << "): "
				  << reinterpret_cast<const char*>(error.data()) << '\n';
	}

	return status;
}
