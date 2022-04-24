add_library (Texture SHARED "Texture.cpp" "lodepng.cpp" Texture.rc Texture.def "TextureWin32.cpp" "Texture.h")
target_include_directories (Texture PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(Texture PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/Plugins"
	SUFFIX ".bep")
target_link_libraries(Texture PRIVATE UABE_Generic UABE_Win32 AssetsTools libStringConverter)
