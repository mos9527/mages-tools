#pragma once

#include <iostream>
#include <string>
#include <ctype.h>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <span>
#include <source_location>
#include "argh.h"
inline void __check(bool condition, const std::string& message = "", const std::source_location& location = std::source_location::current()) {
	if (!condition) {
		std::cerr << "[FATAL] @ " << location.file_name() << ":" << location.line() << " " << location.function_name() << std::endl;
		std::cerr << message << std::endl;
		std::abort();
	}
}
#define CHECK(EXPR, ...) __check(!!(EXPR), __VA_ARGS__)