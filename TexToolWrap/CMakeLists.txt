find_package(PVRTexTool REQUIRED)

add_library (TexToolWrap SHARED "TexToolWrap.cpp" exports.def)
target_include_directories (TexToolWrap PRIVATE ${PVRTexTool_INCLUDE_DIR})

set_target_properties(TexToolWrap PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_link_libraries(TexToolWrap PUBLIC ${PVRTexTool_LIBRARIES})

set(TexToolWrap_MODULE_DEPENDENCY_PATHS ${PVRTexTool_MODULES})

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(depfile ${TexToolWrap_MODULE_DEPENDENCY_PATHS})
	configure_file("${depfile}" "${CMAKE_BINARY_DIR}/bin" COPYONLY)
endforeach()

install(TARGETS TexToolWrap
        RUNTIME
        DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES ${TexToolWrap_MODULE_DEPENDENCY_PATHS}
        DESTINATION ${CMAKE_INSTALL_BINDIR}/
)
