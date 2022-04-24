add_library (libCompression STATIC lz4.c lz4frame.c lz4hc.c xxhash.c lz4dec.cpp lz4enc.cpp LzFind.c LzFindMt.c LzmaDec.c LzmaEnc.c Threads.c)
target_include_directories (libCompression PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
