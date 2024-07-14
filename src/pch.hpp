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
#define PRED(X) [](auto const& lhs, auto const& rhs) {return X;}
#define PAIR2(T) std::pair<T,T>
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
// https://stackoverflow.com/a/14561794
constexpr size_t alignUp(size_t size, size_t alignment) {
	return (size + alignment - 1) & ~(alignment - 1);
}
template<typename T> concept Fundamental = std::is_fundamental_v<T>;
typedef std::vector<uint8_t> u8vec;
struct u8stream {
private:
	size_t pos;
	bool big_endian;
public:
	u8vec& data;
	u8stream(u8vec& data, bool is_big_endian) : data(data), pos(0), big_endian(is_big_endian) {}
	// FILE* like operations
	inline bool is_big_endian() { return big_endian; }
	inline void resize(size_t size) { data.resize(size); }
	inline size_t remain() const {
		return data.size() - pos;
	}
	inline size_t tell() const {
		return pos;
	}
	inline void seek(size_t pos) {
		pos = pos;
	}
	inline void read_at(void* dst, size_t size, size_t offset, bool endianess = false) {
		CHECK(offset + size <= data.size(), "read out of bounds");
		memcpy(dst, data.data() + offset, size);
		if (endianess && big_endian && size > 1) std::reverse((uint8_t*)dst, (uint8_t*)dst + size);
	}
	inline void write_at(void* src, size_t size, size_t offset, bool endianess = false) {
		CHECK(offset + size <= data.size(), "write out of bounds");
		memcpy(data.data() + offset, src, size);
		if (endianess && big_endian && size > 1) std::reverse((uint8_t*)data.data() + offset, (uint8_t*)data.data() + offset + size);
	}
	// Stream operations
	inline void read(void* dst, size_t size, bool endianess = false) {
		read_at(dst, size, tell(), endianess);
		pos += size;
	}
	inline void write(void* src, size_t size, bool endianess = false) {
		write_at(src, size, tell(), endianess);
		pos += size;
	}
	// Fundamental Type shorthands
	template<Fundamental T> inline void read_at(T& dst, size_t offset) {
		read_at(&dst, sizeof(T), offset, true /* Fundamental types takes endianess into account */);
	};
	template<Fundamental T> inline void read(T& dst) {
		read(&dst, sizeof(T), true /* Same here */);
		return;
	};
	template<Fundamental T> inline T read_at(size_t offset) {
		T dst;
		read_at<T>(dst, offset);
		return dst;
	}
	template<Fundamental T> inline T read() {
		T ret = read_at<T>(pos);
		pos += sizeof(T);
		return ret;
	}
	template<Fundamental T> inline u8stream& operator>>(T& value) { read(value); return *this; }
	template<Fundamental T> inline void write_at(T const& src, size_t offset) {
		T data = src;
		write_at(&data, sizeof(T), offset, true  /* Same here */);
	};
	template<Fundamental T> inline void write(T const& src) {
		T data = src;
		write(&data, sizeof(T), true  /* Same here */);
		return;
	};
	template<Fundamental T> inline u8stream& operator<<(T const& value) { write(value); return *this; }
	// Iterators
	inline u8vec::iterator begin() { return data.begin() + pos; }
	inline u8vec::iterator end() { return data.end(); }
};
