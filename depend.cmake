include(FetchContent)

# ASTC Encoder

FetchContent_Declare(
  astcenc
  GIT_REPOSITORY https://github.com/ARM-software/astc-encoder
  GIT_TAG 7e2a81ed5abc202c6f06be9302d193ba44a765c9 #3.5
  BINARY_DIR  "${CMAKE_CURRENT_BINARY_DIR}/_deps/astcenc-build"
  SUBBUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/_deps/astcenc-subbuild"
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fetchcontent/astcenc-src"
)
set(ISA_SSE41 ON CACHE BOOL "")
set(CLI OFF CACHE BOOL "")
if (NOT astcenc_POPULATED)
	FetchContent_Populate(
		astcenc
	)
endif (NOT astcenc_POPULATED)
find_package(Git REQUIRED)
execute_process(COMMAND ${GIT_EXECUTABLE} apply "${CMAKE_CURRENT_SOURCE_DIR}/fetchcontent/astcenc-x86-32-popcntu64-patch-7e2a81ed5abc202c6f06be9302d193ba44a765c9.patch"
	            WORKING_DIRECTORY ${astcenc_SOURCE_DIR}
	            ERROR_QUIET)
add_subdirectory(${astcenc_SOURCE_DIR} ${astcenc_BINARY_DIR})

# Crunch (Unity fork, compatible with the formats used in Unity 2017.3 onwards)

FetchContent_Declare(
  crunch-unity
  GIT_REPOSITORY https://github.com/Unity-Technologies/crunch
  GIT_TAG 8708900eca8ec609d279270e72936258f81ddfb7
  BINARY_DIR  "${CMAKE_CURRENT_BINARY_DIR}/_deps/crunch-unity-build"
  SUBBUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/_deps/crunch-unity-subbuild"
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fetchcontent/crunch-unity-src"
)
if (NOT crunch-unity_POPULATED)
	FetchContent_Populate(crunch-unity)
endif (NOT crunch-unity_POPULATED)
find_package(Git REQUIRED)
execute_process(COMMAND ${GIT_EXECUTABLE} apply "${CMAKE_CURRENT_SOURCE_DIR}/fetchcontent/crunch-unity-patch-8708900eca8ec609d279270e72936258f81ddfb7.patch"
	            WORKING_DIRECTORY ${crunch-unity_SOURCE_DIR}
	            ERROR_QUIET)
#Check if the patch is applied.
execute_process(COMMAND ${GIT_EXECUTABLE} apply --reverse --check "${CMAKE_CURRENT_SOURCE_DIR}/fetchcontent/crunch-unity-patch-8708900eca8ec609d279270e72936258f81ddfb7.patch"
				WORKING_DIRECTORY ${crunch-unity_SOURCE_DIR})

file(GLOB crnlib-unity_SOURCES_EXCLUDE "${crunch-unity_SOURCE_DIR}/crnlib/lzham*.cpp")
file(GLOB crnlib-unity_SOURCES "${crunch-unity_SOURCE_DIR}/crnlib/*.cpp")
list(REMOVE_ITEM crnlib-unity_SOURCES ${crnlib-unity_SOURCES_EXCLUDE})
add_library (crnlib-unity STATIC ${crnlib-unity_SOURCES})
target_include_directories (crnlib-unity PUBLIC "${crunch-unity_SOURCE_DIR}/inc")

# Crunch (older version, compatible with the formats used in Unity 5 .. 2017.2)

FetchContent_Declare(
  crunch-legacy
  GIT_REPOSITORY https://github.com/BinomialLLC/crunch
  GIT_TAG 671a0648c8a440b4397f1d96ea5cf5700f830417
  BINARY_DIR  "${CMAKE_CURRENT_BINARY_DIR}/_deps/crunch-legacy-build"
  SUBBUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/_deps/crunch-legacy-subbuild"
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fetchcontent/crunch-legacy-src"
)
if (NOT crunch-legacy_POPULATED)
	FetchContent_Populate(crunch-legacy)
endif (NOT crunch-legacy_POPULATED)
find_package(Git REQUIRED)
execute_process(COMMAND ${GIT_EXECUTABLE} apply "${CMAKE_CURRENT_SOURCE_DIR}/fetchcontent/crunch-legacy-patch-671a0648c8a440b4397f1d96ea5cf5700f830417.patch"
	            WORKING_DIRECTORY ${crunch-legacy_SOURCE_DIR}
	            ERROR_QUIET)
#Check if the patch is applied.
execute_process(COMMAND ${GIT_EXECUTABLE} apply --reverse --check "${CMAKE_CURRENT_SOURCE_DIR}/fetchcontent/crunch-legacy-patch-671a0648c8a440b4397f1d96ea5cf5700f830417.patch"
				WORKING_DIRECTORY ${crunch-legacy_SOURCE_DIR})

file(GLOB crnlib-legacy_SOURCES_EXCLUDE "${crunch-legacy_SOURCE_DIR}/crnlib/lzham*.cpp")
file(GLOB crnlib-legacy_SOURCES ${crunch-legacy_SOURCE_DIR}/crnlib/*.cpp)
list(REMOVE_ITEM crnlib-legacy_SOURCES ${crnlib-legacy_SOURCES_EXCLUDE})
add_library (crnlib-legacy STATIC ${crnlib-legacy_SOURCES})
target_include_directories (crnlib-legacy PUBLIC "${crunch-legacy_SOURCE_DIR}/inc")

# Squish (Official repo appears to be https://sourceforge.net/projects/libsquish/ , previously on Google Code)

FetchContent_Declare(
  libsquish
  URL "${CMAKE_CURRENT_SOURCE_DIR}/fetchcontent/libsquish-1.15.tgz"
  URL_HASH SHA256=628796EEBA608866183A61D080D46967C9DDA6723BC0A3EC52324C85D2147269
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fetchcontent/libsquish-src"
)
if (NOT libsquish_POPULATED)
	FetchContent_Populate(libsquish)
endif (NOT libsquish_POPULATED)
set(BUILD_SQUISH_WITH_OPENMP OFF CACHE BOOL "")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/fetchcontent/libsquish-src" "${CMAKE_CURRENT_BINARY_DIR}/_deps/libsquish-build")
set(LIBSQUISH_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fetchcontent/libsquish-src")

# ISPC Texture Compressor
#  Requires the ISPC compiler (https://github.com/ispc/ispc) to generate some object files.
# Official Windows Binaries: https://github.com/ispc/ispc/releases/download/v1.17.0/ispc-v1.17.0-windows.zip
# Binaries exist for other platforms, assuming Windows for now. Could also be compiled from scratch instead.

FetchContent_Declare(
  ispc_texcomp
  GIT_REPOSITORY https://github.com/GameTechDev/ISPCTextureCompressor
  GIT_TAG 14d998c02b71c356ff3a1ec1adc9243a517bbf38
  BINARY_DIR  "${CMAKE_CURRENT_BINARY_DIR}/_deps/ispc_texcomp-build"
  SUBBUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/_deps/ispc_texcomp-subbuild"
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fetchcontent/ispc_texcomp-src"
)
set(ISA_SSE41 ON CACHE BOOL "")
set(CLI OFF CACHE BOOL "")
if (NOT ispc_texcomp_POPULATED)
	FetchContent_Populate(ispc_texcomp)
endif (NOT ispc_texcomp_POPULATED)
# Fetch the ISPC compiler binaries
FetchContent_Declare(
  ispc_compiler_binaries
  URL https://github.com/ispc/ispc/releases/download/v1.17.0/ispc-v1.17.0-windows.zip
  URL_HASH SHA256=E9A7CC98F69357482985BCBF69FA006632CEE7B3606069B4D5E16DC62092D660
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fetchcontent/ispc_compiler"
)
if (NOT ispc_compiler_binaries_POPULATED)
	FetchContent_Populate(ispc_compiler_binaries)
endif (NOT ispc_compiler_binaries_POPULATED)

# Custom command to invoke the ISPC compiler.
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(ISPC_COMPILER_COMMAND_ARCH "")
elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
	set(ISPC_COMPILER_COMMAND_ARCH "--arch=x86")
endif ()
add_custom_command(
	OUTPUT
		${ispc_texcomp_BINARY_DIR}/kernel.obj
		${ispc_texcomp_BINARY_DIR}/kernel_sse4.obj
		${ispc_texcomp_BINARY_DIR}/kernel_avx.obj
		${ispc_texcomp_BINARY_DIR}/kernel_ispc.h

		${ispc_texcomp_BINARY_DIR}/kernel_astc.obj
		${ispc_texcomp_BINARY_DIR}/kernel_astc_sse4.obj
		${ispc_texcomp_BINARY_DIR}/kernel_astc_avx.obj
		${ispc_texcomp_BINARY_DIR}/kernel_astc_ispc.h
	COMMAND
		"${ispc_compiler_binaries_SOURCE_DIR}/bin/ispc.exe" -O2 "${ispc_texcomp_SOURCE_DIR}/ispc_texcomp/kernel.ispc"
		-o "${ispc_texcomp_BINARY_DIR}/kernel.obj"
		-h "${ispc_texcomp_BINARY_DIR}/kernel_ispc.h"
		${ISPC_COMPILER_COMMAND_ARCH}
		--target=sse4,avx
		--opt=fast-math
	COMMAND
		"${ispc_compiler_binaries_SOURCE_DIR}/bin/ispc.exe" -O2 "${ispc_texcomp_SOURCE_DIR}/ispc_texcomp/kernel_astc.ispc"
		-o "${ispc_texcomp_BINARY_DIR}/kernel_astc.obj"
		-h "${ispc_texcomp_BINARY_DIR}/kernel_astc_ispc.h"
		${ISPC_COMPILER_COMMAND_ARCH}
		--target=sse4,avx
		--opt=fast-math
	WORKING_DIRECTORY "${ispc_texcomp_BINARY_DIR}"
)
# Create the actual ispc_texcomp library.
add_library (ispc_texcomp SHARED
	${ispc_texcomp_SOURCE_DIR}/ispc_texcomp/ispc_texcomp.cpp
	${ispc_texcomp_SOURCE_DIR}/ispc_texcomp/ispc_texcomp_astc.cpp
	${ispc_texcomp_SOURCE_DIR}/ispc_texcomp/ispc_texcomp.def
	${ispc_texcomp_BINARY_DIR}/kernel.obj
	${ispc_texcomp_BINARY_DIR}/kernel_sse4.obj
	${ispc_texcomp_BINARY_DIR}/kernel_avx.obj
	${ispc_texcomp_BINARY_DIR}/kernel_astc.obj
	${ispc_texcomp_BINARY_DIR}/kernel_astc_sse4.obj
	${ispc_texcomp_BINARY_DIR}/kernel_astc_avx.obj
)
target_include_directories (ispc_texcomp PUBLIC "${ispc_texcomp_SOURCE_DIR}/ispc_texcomp")
target_include_directories (ispc_texcomp PRIVATE "${ispc_texcomp_BINARY_DIR}")
set_target_properties(ispc_texcomp PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# texgenpack (only used for decompression) and dependencies

FetchContent_Declare(
  pthreads4w
  GIT_REPOSITORY https://github.com/jwinarske/pthreads4w
  GIT_TAG 02fecc211d626f28e05ecbb0c10f739bd36d6442 #2.10.0 RC
  BINARY_DIR  "${CMAKE_CURRENT_BINARY_DIR}/_deps/pthreads4w-build"
  SUBBUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/_deps/pthreads4w-subbuild"
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fetchcontent/pthreads4w-src"
)
if (NOT pthreads4w_POPULATED)
	FetchContent_Populate(pthreads4w)
endif (NOT pthreads4w_POPULATED)
FetchContent_Declare(
  libfgen
  GIT_REPOSITORY https://github.com/hglm/libfgen
  GIT_TAG 071e5130f5286850eafe8de65f51e05604a02929
  BINARY_DIR  "${CMAKE_CURRENT_BINARY_DIR}/_deps/libfgen-build"
  SUBBUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/_deps/libfgen-subbuild"
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fetchcontent/libfgen-src"
)
if (NOT libfgen_POPULATED)
	FetchContent_Populate(libfgen)
endif (NOT libfgen_POPULATED)
FetchContent_Declare(
  texgenpack
  GIT_REPOSITORY https://github.com/hglm/texgenpack
  GIT_TAG cf548ef583ca9592a55ea217b0ec43a2e25b9cbe #0.96
  BINARY_DIR  "${CMAKE_CURRENT_BINARY_DIR}/_deps/texgenpack-build"
  SUBBUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/_deps/texgenpack-subbuild"
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fetchcontent/texgenpack-src"
)
if (NOT texgenpack_POPULATED)
	FetchContent_Populate(texgenpack)
endif (NOT texgenpack_POPULATED)
find_package(Git REQUIRED)
execute_process(COMMAND ${GIT_EXECUTABLE} apply "${CMAKE_CURRENT_SOURCE_DIR}/fetchcontent/texgenpack-patch-cf548ef583ca9592a55ea217b0ec43a2e25b9cbe.patch"
				WORKING_DIRECTORY ${texgenpack_SOURCE_DIR}
				ERROR_QUIET)
execute_process(COMMAND ${GIT_EXECUTABLE} apply "${CMAKE_CURRENT_SOURCE_DIR}/fetchcontent/libfgen-patch-071e5130f5286850eafe8de65f51e05604a02929.patch"
				WORKING_DIRECTORY ${libfgen_SOURCE_DIR}
				ERROR_QUIET)
execute_process(COMMAND ${GIT_EXECUTABLE} apply "${CMAKE_CURRENT_SOURCE_DIR}/fetchcontent/pthreads4w-patch-02fecc211d626f28e05ecbb0c10f739bd36d6442.patch"
				WORKING_DIRECTORY ${pthreads4w_SOURCE_DIR}
				ERROR_QUIET)
# Verify that the patches have been applied.
execute_process(COMMAND ${GIT_EXECUTABLE} apply --reverse --check "${CMAKE_CURRENT_SOURCE_DIR}/fetchcontent/texgenpack-patch-cf548ef583ca9592a55ea217b0ec43a2e25b9cbe.patch"
				WORKING_DIRECTORY ${texgenpack_SOURCE_DIR})
execute_process(COMMAND ${GIT_EXECUTABLE} apply --reverse --check "${CMAKE_CURRENT_SOURCE_DIR}/fetchcontent/libfgen-patch-071e5130f5286850eafe8de65f51e05604a02929.patch"
				WORKING_DIRECTORY ${libfgen_SOURCE_DIR})
execute_process(COMMAND ${GIT_EXECUTABLE} apply --reverse --check "${CMAKE_CURRENT_SOURCE_DIR}/fetchcontent/pthreads4w-patch-02fecc211d626f28e05ecbb0c10f739bd36d6442.patch"
				WORKING_DIRECTORY ${pthreads4w_SOURCE_DIR})

add_library (pthreads4w STATIC "${pthreads4w_SOURCE_DIR}/pthread.c")
target_include_directories (pthreads4w PUBLIC "${pthreads4w_SOURCE_DIR}")
target_compile_definitions(pthreads4w PRIVATE PTW32_STATIC_LIB HAVE_CONFIG_H)
add_library (libfgen STATIC
	"${libfgen_SOURCE_DIR}/bitstring.c" "${libfgen_SOURCE_DIR}/cache.c" "${libfgen_SOURCE_DIR}/crossover.c"
	"${libfgen_SOURCE_DIR}/decode.c" "${libfgen_SOURCE_DIR}/error.c" "${libfgen_SOURCE_DIR}/ffit.c"
	"${libfgen_SOURCE_DIR}/ga.c" "${libfgen_SOURCE_DIR}/gray.c" "${libfgen_SOURCE_DIR}/migration.c"
	"${libfgen_SOURCE_DIR}/mutation.c" "${libfgen_SOURCE_DIR}/parameters.c" "${libfgen_SOURCE_DIR}/population.c"
	"${libfgen_SOURCE_DIR}/pso.c" "${libfgen_SOURCE_DIR}/random.c" "${libfgen_SOURCE_DIR}/seed.c"
	"${libfgen_SOURCE_DIR}/selection.c" "${libfgen_SOURCE_DIR}/steady_state.c"
)
target_include_directories (libfgen PUBLIC "${libfgen_SOURCE_DIR}")
target_link_libraries(libfgen PUBLIC pthreads4w)
add_library (texgenpack SHARED
	"${texgenpack_SOURCE_DIR}/astc.c" "${texgenpack_SOURCE_DIR}/bptc.c" "${texgenpack_SOURCE_DIR}/calibrate.c"
	"${texgenpack_SOURCE_DIR}/compare.c" "${texgenpack_SOURCE_DIR}/compress.c" "${texgenpack_SOURCE_DIR}/dxtc.c"
	"${texgenpack_SOURCE_DIR}/etc2.c" "${texgenpack_SOURCE_DIR}/file.c" "${texgenpack_SOURCE_DIR}/half_float.c"
	"${texgenpack_SOURCE_DIR}/image.c" "${texgenpack_SOURCE_DIR}/mipmap.c" "${texgenpack_SOURCE_DIR}/rgtc.c"
	"${texgenpack_SOURCE_DIR}/texgenpack.c" "${texgenpack_SOURCE_DIR}/texture.c"
)
target_include_directories (texgenpack PUBLIC "${texgenpack_SOURCE_DIR}")
target_link_libraries(texgenpack PRIVATE libfgen pthreads4w)
target_compile_definitions(texgenpack PRIVATE TEXGENPACK_EXPORTS)
set_target_properties(texgenpack PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# mCtrl, with patches for UABE (customized TreeList and Mditab controls)
# Note: The patches disable some features of mCtrl to save miniscule amounts of space.

set(MCTRL_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MCTRL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  mctrl
  GIT_REPOSITORY https://github.com/mity/mctrl
  GIT_TAG 42334bfbfffbb1530e69213199e775e54edbad21 #release-0.11.5
  BINARY_DIR  "${CMAKE_CURRENT_BINARY_DIR}/_deps/mctrl-build"
  SUBBUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/_deps/mctrl-subbuild"
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fetchcontent/mctrl-src"
)
if (NOT mctrl_POPULATED)
	FetchContent_Populate(mctrl)
endif (NOT mctrl_POPULATED)
find_package(Git REQUIRED)
execute_process(COMMAND ${GIT_EXECUTABLE} apply "${CMAKE_CURRENT_SOURCE_DIR}/fetchcontent/mctrl-patch-42334bfbfffbb1530e69213199e775e54edbad21.patch"
				WORKING_DIRECTORY ${mctrl_SOURCE_DIR}
				ERROR_QUIET)
# Verify that the patch has been applied.
execute_process(COMMAND ${GIT_EXECUTABLE} apply --reverse --check "${CMAKE_CURRENT_SOURCE_DIR}/fetchcontent/mctrl-patch-42334bfbfffbb1530e69213199e775e54edbad21.patch"
				WORKING_DIRECTORY ${mctrl_SOURCE_DIR})
add_subdirectory(${mctrl_SOURCE_DIR} ${mctrl_BINARY_DIR})
set(MCTRL_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fetchcontent/mctrl-src/include")

# jsmn

FetchContent_Declare(
  jsmn
  GIT_REPOSITORY https://github.com/zserge/jsmn
  GIT_TAG 25647e692c7906b96ffd2b05ca54c097948e879c
  BINARY_DIR  "${CMAKE_CURRENT_BINARY_DIR}/_deps/jsmn-build"
  SUBBUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/_deps/jsmn-subbuild"
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fetchcontent/jsmn-src"
)
if (NOT jsmn_POPULATED)
	FetchContent_Populate(jsmn)
endif (NOT jsmn_POPULATED)
add_library (jsmn INTERFACE)
target_include_directories (jsmn INTERFACE "${jsmn_SOURCE_DIR}")

# assimp

FetchContent_Declare(
  assimp
  GIT_REPOSITORY https://github.com/assimp/assimp
  GIT_TAG 80799bdbf90ce626475635815ee18537718a05b1 #4.1.0
  BINARY_DIR  "${CMAKE_CURRENT_BINARY_DIR}/_deps/assimp-build"
  SUBBUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/_deps/assimp-subbuild"
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fetchcontent/assimp-src"
)
if (NOT assimp_POPULATED)
	FetchContent_Populate(assimp)
endif (NOT assimp_POPULATED)
find_package(Git REQUIRED)
execute_process(COMMAND ${GIT_EXECUTABLE} apply "${CMAKE_CURRENT_SOURCE_DIR}/fetchcontent/assimp-patch-80799bdbf90ce626475635815ee18537718a05b1.patch"
				WORKING_DIRECTORY ${assimp_SOURCE_DIR}
				ERROR_QUIET)
# Verify that the patch has been applied.
execute_process(COMMAND ${GIT_EXECUTABLE} apply --reverse --check "${CMAKE_CURRENT_SOURCE_DIR}/fetchcontent/assimp-patch-80799bdbf90ce626475635815ee18537718a05b1.patch"
				WORKING_DIRECTORY ${assimp_SOURCE_DIR})
set(BUILD_SHARED_LIBS OFF CACHE BOOL "")
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "")
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "")
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "")
set(ASSIMP_BUILD_COLLADA_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_3MF_IMPORTER ON CACHE BOOL "" FORCE) # Required to prevent linker errors. Something apparently uses this importer.
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/fetchcontent/assimp-src" "${CMAKE_CURRENT_BINARY_DIR}/_deps/assimp-build")
set(ASSIMP_INCLUDE_DIR "${assimp_SOURCE_DIR}/include" "${assimp_BINARY_DIR}/include" "${assimp_SOURCE_DIR}/code")
SET(CMAKE_DEBUG_POSTFIX "" CACHE STRING "" FORCE)
