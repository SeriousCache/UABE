add_library (Font SHARED "Font.cpp" Font.def)
target_include_directories (Font PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(Font PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/Plugins"
	SUFFIX ".bep")
target_link_libraries(Font PUBLIC UABE_Generic AssetsTools libStringConverter)
