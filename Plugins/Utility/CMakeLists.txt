add_library (Utility SHARED "Utility.cpp" Utility.def)
target_include_directories (Utility PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(Utility PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/Plugins"
	SUFFIX ".bep")
target_link_libraries(Utility PRIVATE UABE_Win32 AssetsTools libStringConverter)
