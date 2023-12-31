set(WIN32_EXECUTABLE TRUE)

add_executable (AssetBundleExtractor WIN32 "AssetBundleExtractor.cpp" Launcher.rc "resource.h")

target_link_libraries (AssetBundleExtractor LINK_PUBLIC UABE_Win32)

set_target_properties(AssetBundleExtractor PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

set(AssetBundleExtractor_MODULE_DEPENDENCY_PATHS ${MCTRL_MODULES} ${ISPC_TEXCOMP_MODULES} ${TEXGENPACK_MODULES})

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(depfile ${AssetBundleExtractor_MODULE_DEPENDENCY_PATHS})
	configure_file("${depfile}" "${CMAKE_BINARY_DIR}/bin" COPYONLY)
endforeach()
configure_file("${UABE_ROOT}/classdata.tpk" "${CMAKE_BINARY_DIR}/bin" COPYONLY)

configure_file("${UABE_ROOT}/Readme.License.txt" "${CMAKE_BINARY_DIR}/bin" COPYONLY)
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/bin/Licenses")
add_custom_command(TARGET AssetBundleExtractor PRE_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory "${UABE_ROOT}/Licenses" "${CMAKE_BINARY_DIR}/bin/Licenses")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/bin/Tools")
add_custom_command(TARGET AssetBundleExtractor PRE_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory "${UABE_ROOT}/Tools" "${CMAKE_BINARY_DIR}/bin/Tools")
