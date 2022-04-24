//Custom LZ4 decompression with data streams based on the original algorithm.

#pragma once
#include "lz4e.h"

//Returns 1 on success. Returns 0 if it failed to write the final data or a negative value if it failed to read/write data.
int LZ4e_decompress_safe(char* sourceBuf, char* destBuf, int sourceBufSize, int destBufSize, LZ4e_instream_t* inStream, LZ4e_outstream_t* outStream);