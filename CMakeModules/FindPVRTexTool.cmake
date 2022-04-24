set(PVRTexTool_ROOT CACHE PATH "Root directory of PVRTexTool")

if (CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(PVRTexTool_NAME_ARCH "x86_64")
elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
	set(PVRTexTool_NAME_ARCH "x86_32")
endif (CMAKE_SIZEOF_VOID_P EQUAL 8)

find_path(PVRTexTool_INCLUDE_DIR NAMES PVRTexLib.h PVRTexLib.hpp PATHS ${PVRTexTool_ROOT}/Library/Include)
find_library(PVRTexTool_LIBRARIES NAMES PVRTexLib.lib PATHS ${PVRTexTool_ROOT}/Library/Windows_${PVRTexTool_NAME_ARCH})
find_path(PVRTexTool_MODULE_DIR NAMES PVRTexLib.dll PATHS ${PVRTexTool_ROOT}/Library/Windows_${PVRTexTool_NAME_ARCH})
set(PVRTexTool_MODULES "${PVRTexTool_MODULE_DIR}/PVRTexLib.dll")

if (PVRTexTool_INCLUDE_DIR AND PVRTexTool_LIBRARIES AND PVRTexTool_MODULES)
   set(PVRTexTool_FOUND TRUE)
   message(STATUS "Found PVRTexTool: ${PVRTexTool_ROOT}")
endif (PVRTexTool_INCLUDE_DIR AND PVRTexTool_LIBRARIES AND PVRTexTool_MODULES)
