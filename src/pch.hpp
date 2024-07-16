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
		std::cerr << location.file_name() << ":" << location.line() << ",at " << location.function_name() << std::endl;		
		if (message.size()) std::cerr << "ERROR: " << message << std::endl;
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
inline void dump_memory(const char* fname, void* src, size_t size) {
	FILE* f = fopen(fname, "wb");
	fwrite(src, size, 1, f);
	fclose(f);
}
template<typename T> concept Fundamental = std::is_fundamental_v<T>;
typedef std::vector<uint8_t> u8vec;
// Owning u8vec wrapper with stream operations
struct u8stream {
private:
	size_t pos;
	bool big_endian;
public:
	u8vec buffer;
	// Owning data. Initializes with a given size.
	u8stream(size_t init_size, bool is_big_endian) : buffer(init_size), pos(0), big_endian(is_big_endian) {}
	// Owning data. The source buffer is destroyed.
	u8stream(u8vec&& buffer, bool is_big_endian) : buffer(buffer), pos(0), big_endian(is_big_endian) {}
	// Non-owning (copying) stream. The data is copied and owned by the stream. The source buffer is not destroyed.
	u8stream(u8vec const& buffer, bool is_big_endian) : buffer(buffer), pos(0), big_endian(is_big_endian) {}
	inline u8vec::pointer data() { return buffer.data(); }
	inline size_t size() const { return buffer.size(); }
	// FILE* like operations
	inline bool is_big_endian() const { return big_endian; }
	inline void resize(size_t size) { buffer.resize(size); }
	inline size_t remain() const {
		return buffer.size() - pos;
	}
	inline size_t tell() const {
		return pos;
	}
	inline void seek(size_t npos) {
		pos = npos;
		buffer.resize(std::max(buffer.size(), pos));
	}
	inline size_t read_at(void* dst, size_t size, size_t offset, bool endianess = false) {
		size_t size_read = std::min(size, buffer.size() - offset);
		memcpy(dst, buffer.data() + offset, size_read);
		if (endianess && big_endian && size > 1) std::reverse((uint8_t*)dst, (uint8_t*)dst + size_read);
		return size_read;
	}
	inline size_t write_at(void* src, size_t size, size_t offset, bool endianess = false) {
		buffer.resize(std::max(buffer.size(), offset + size));
		memcpy(buffer.data() + offset, src, size);
		if (endianess && big_endian && size > 1) std::reverse((uint8_t*)buffer.data() + offset, (uint8_t*)buffer.data() + offset + size);
		return size;
	}
	// Stream operations
	inline size_t read(void* dst, size_t size, bool endianess = false) {
		size_t size_read = read_at(dst, size, tell(), endianess);
		pos += size;
		return size_read;
	}
	inline size_t write(void* src, size_t size, bool endianess = false) {
		size = write_at(src, size, tell(), endianess);
		pos += size;
		return size;
	}
	// Fundamental Type shorthands
	template<Fundamental T> inline void read_at(T& dst, size_t offset) {
		CHECK(read_at(&dst, sizeof(T), offset, true /* Fundamental types takes endianess into account */) == sizeof(T));
	};
	template<Fundamental T> inline void read(T& dst) {
		CHECK(read(&dst, sizeof(T), true /* Same here */) == sizeof(T));
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
	template<Fundamental T> inline void write_at(T const& src, size_t offset) {
		T buffer = src;
		write_at(&buffer, sizeof(T), offset, true  /* Same here */);
	};
	template<Fundamental T> inline void write(T const& src) {
		T buffer = src;
		write(&buffer, sizeof(T), true  /* Same here */);
		return;
	};
	// Stream operators
	template<typename T> inline u8stream& operator>>(T& value) requires std::is_same_v<T, u8vec> 
	{ 
		value.resize(remain()); 
		read((void*)value.data(), value.size(), false);
		return *this; 
	}
	template<Fundamental T> inline u8stream& operator>>(T& value) { read(value); return *this; }
	template<typename T> inline u8stream& operator<<(T const& value) requires std::is_same_v<T, u8vec> 
	{ 
		write((void*)value.data(), value.size(), false); 
		return *this; 
	}
	template<Fundamental T> inline u8stream& operator<<(T const& value) { write(value); return *this; }
	// Iterators
	inline u8vec::iterator begin() { return buffer.begin() + pos; }
	inline u8vec::iterator end() { return buffer.end(); }
};
