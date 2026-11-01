// license:BSD-3-Clause
// copyright-holders:Aaron Giles

#include <iostream>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#define osd_get_command_line chdman_dll_get_command_line
#define main chdman_command_main
#include "chdman.cpp"
#undef main
#undef osd_get_command_line

std::vector<std::string> chdman_dll_get_command_line(int argc, char *argv[])
{
	std::vector<std::string> arguments;
	arguments.reserve(argc);
	for (int index = 0; index < argc; ++index)
		arguments.emplace_back(argv[index]);
	return arguments;
}

extern "C" __declspec(dllexport) int chdman_execute(
	int argc,
	const char *const *argv,
	char *error_buffer,
	size_t error_capacity)
{
	if (argc < 1 || argv == nullptr)
		return 2;

	std::ostringstream output;
	std::streambuf *old_out = std::cout.rdbuf(output.rdbuf());
	std::streambuf *old_error = std::cerr.rdbuf(output.rdbuf());
	std::vector<char *> mutable_argv;
	mutable_argv.reserve(argc);
	for (int index = 0; index < argc; ++index)
		mutable_argv.push_back(const_cast<char *>(argv[index]));

	int result;
	try
	{
		result = chdman_command_main(argc, mutable_argv.data());
	}
	catch (...)
	{
		result = 1;
		output << "CHDMAN threw an unhandled exception";
	}

	std::cout.rdbuf(old_out);
	std::cerr.rdbuf(old_error);
	const std::string message = output.str();
	if (error_buffer != nullptr && error_capacity > 0)
	{
		const size_t length = std::min(message.size(), error_capacity - 1);
		const size_t offset = message.size() - length;
		std::memcpy(error_buffer, message.data() + offset, length);
		error_buffer[length] = '\0';
	}
	return result;
}
