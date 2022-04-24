#pragma once
#include "lz4e.h"

unsigned int LZ4e_compress_fast(LZ4e_instream_t* source, LZ4e_outstream_t* dest, int acceleration, unsigned int totalSize = (unsigned int)-1, unsigned int streamBufSize = 8519680);