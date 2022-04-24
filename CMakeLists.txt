cmake_minimum_required (VERSION 3.18)
project (AssetBundleExtractor)
set(UABE_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

#set(THREADS_PREFER_PTHREAD_FLAG ON)
#find_package(Threads REQUIRED)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/CMakeModules")
#find_package(ispc_texcomp REQUIRED)
#find_package(texgenpack REQUIRED)
#find_package(mCtrl REQUIRED)
#find_package(jsmn REQUIRED)

#Enable C++20 support
set(CMAKE_CXX_STANDARD 23) #23, since MSVC++ no longer appears to enable std::format with standard 20.
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -D_DEBUG")

add_compile_options(/W3)
add_definitions(-DUNICODE)
add_definitions(-D_UNICODE)

include(depend.cmake)

add_subdirectory (Launcher)
add_subdirectory (libCompression)
add_subdirectory (libStringConverter)
add_subdirectory (CrnlibWrap)
add_subdirectory (AssetsTools)
add_subdirectory (ModInstaller)
add_subdirectory (UABE_Generic)
add_subdirectory (UABE_Win32)
add_subdirectory (Plugins)
add_subdirectory (TexToolWrap)
