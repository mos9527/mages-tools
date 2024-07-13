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
template<typename T> concept Fundamental = std::is_fundamental_v<T>;
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
		pos = pos;
	}
	inline void read_at(void* dst, size_t size, size_t offset) {
		CHECK(offset + size <= src.size(), "read out of bounds");
		memcpy(dst, src.data() + offset, size);
		// We assume the code runs on a little-endian machine
		if (big_endian && size > 1) std::reverse((uint8_t*)dst, (uint8_t*)dst + size);		
	}
	inline void read(void* dst, size_t size) {
		read_at(dst, size, tell());
		pos += size;		
	}	
	template<Fundamental T> inline void read_at(T& dst, size_t offset) {
		read_at(&dst, sizeof(T), offset);
	};
	template<Fundamental T> inline void read(T& dst) {
		read(&dst, sizeof(T));
		return;
	};
	template<Fundamental T> inline T read_at(size_t offset) {
		T dst;
		read_at(dst, offset);
		return dst;
	}
	template<Fundamental T> inline T read() {
		T ret = read_at<T>(pos);
		pos += sizeof(T);
		return ret;
	}	
	template<Fundamental T> inline u8stream& operator>>(T& value) { read(value); return *this; }
}; 