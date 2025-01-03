#include <iostream>
#include <string>
#include <cstdarg>
#include <memory>

#include "utils.h"

std::string cm01::string_format(const std::string& format, ...) {
  std::va_list va, va2;

  va_start(va, format);
  va_copy(va2, va);
	int size_s = std::vsnprintf(nullptr, 0, format.c_str(), va) + 1;
  va_end(va);
	if (size_s <= 0) { throw std::runtime_error("Error during formatting."); }
	auto size = static_cast<size_t>(size_s);
	std::unique_ptr<char[]> buf(new char[size]);
	std::vsnprintf(buf.get(), size, format.c_str(), va2);
  va_end(va2);
	return std::string(buf.get(), buf.get() + size - 1); // We don't want the '\0' inside
}

// // Function for logging with arguments severity, caller function name, line number, printf-like arguments, and an user variable
// void log(int severity, const char* caller_func_name, int line_number, const char* format, ...) {
//   // Get the current time and date
//   auto now = std::chrono::system_clock::now();
//   auto now_c = std::chrono::system_clock::to_time_t(now);
//   std::string now_str = std::ctime(&now_c);

//   // Format the log message
//   char buffer[1024];
//   va_list args;
//   va_start(args, format);
//   vsnprintf(buffer, sizeof(buffer), format, args);
//   va_end(args);

//   // Output the log message to standard output or a file
//   std::cout
//     << "[" << now_str << "]"
//     << "[" << caller_func_name << "#" << line_number << "]"
//     << "[" << severity << "]"
//     << buffer << std::endl;
// }
