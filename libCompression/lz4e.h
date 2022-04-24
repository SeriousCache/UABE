#pragma once
#include <stdint.h>

//Returns the number of bytes read. Must not return negative values.
typedef int(_cdecl *LZ4e_read_callback_t)(void *buffer, int size, struct LZ4e_instream_t *stream);

struct LZ4e_instream_t {
	uint64_t pos; //0 must stand for the first input byte
	LZ4e_read_callback_t callback;
	void *user;
};

//Returns the number of bytes written. Must not return negative values.
typedef int(_cdecl *LZ4e_write_callback_t)(const void *buffer, int size, struct LZ4e_outstream_t *stream);

struct LZ4e_outstream_t {
	LZ4e_write_callback_t callback;
	void *user;
};