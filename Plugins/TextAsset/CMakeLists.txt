add_library (TextAsset SHARED "TextAsset.cpp" TextAsset.def)
target_include_directories (TextAsset PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(TextAsset PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/Plugins"
	SUFFIX ".bep")
target_link_libraries(TextAsset PUBLIC UABE_Generic AssetsTools libStringConverter)
