#pragma once

#include <iostream>
#include <string>
#include <ctype.h>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <span>
#include <source_location>
#include <variant>
#include "argh.h"
inline void __check(bool condition, const std::string& message = "", const std::source_location& location = std::source_location::current()) {
	if (!condition) {
		std::cerr << "[FATAL] @ " << location.file_name() << ":" << location.line() << " " << location.function_name() << std::endl;
		std::cerr << message << std::endl;
		std::abort();
	}
}
#define CHECK(EXPR, ...) __check(!!(EXPR), __VA_ARGS__)
constexpr uint32_t fourCC(const char a, const char b, const char c, const char d) {
	return (a << 0) | (b << 8) | (c << 16) | (d << 24);
};
consteval uint32_t fourCCBig(const char a, const char b, const char c, const char d) {
	return (a << 24) | (b << 16) | (c << 8) | (d << 0);
};
typedef std::vector<uint8_t> u8vec;
struct u8stream {
	u8vec& src;
	size_t pos;
	bool big_endian;
	u8stream(u8vec& src, bool is_big_endian) : src(src), pos(0), big_endian(is_big_endian) {}	
	inline size_t tell() const {
		return pos;
	}
	inline void seek(size_t pos) {
		this->pos = pos;
	}
	inline void read(void* dst, size_t size) {
		CHECK(pos + size <= src.size(), "read out of bounds");
		memcpy(dst, src.data() + pos, size);
		// We assume the code runs on a little-endian machine
		if (big_endian) std::reverse((uint8_t*)dst, (uint8_t*)dst + size);
		pos += size;
	}
	template<typename T> inline void read(T& dst) requires std::is_fundamental_v<T> {
		read(&dst, sizeof(T));
	};
	template<typename T> inline T read() requires std::is_fundamental_v<T> {
		T dst;
		read(dst);
		return dst;
	}
	template<typename T> inline std::string read() requires std::is_same_v<T, std::string> {
		std::string dst;
		while (src[pos]) {
			dst.push_back(read<char>());
		}
		return dst;
	}
	inline void write(const void* src, size_t size) {
		if (pos + size > this->src.size()) {
			this->src.resize(pos + size);
		}
		memcpy(this->src.data() + pos, src, size);
		pos += size;
	}
	template<typename T> inline void write(const T& src) requires std::is_fundamental_v<T> {
		write(&src, sizeof(T));
	}
};