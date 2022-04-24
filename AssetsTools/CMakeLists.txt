add_library (AssetsTools_Static STATIC AssetBundleFileFormat.cpp AssetBundleFileTable.cpp AssetsFileFormat.cpp AssetsFileReader.cpp AssetsFileTable.cpp AssetsReplacer.cpp AssetTypeClass.cpp BundleReplacer.cpp ClassDatabaseFile.cpp EngineVersion.cpp ResourceManagerFile.cpp)
target_include_directories (AssetsTools_Static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(AssetsTools_Static PUBLIC ASSETSTOOLS_IMPORTSTATIC)
target_link_libraries(AssetsTools_Static PUBLIC libCompression libStringConverter)

add_library (AssetsTools SHARED AssetBundleFileFormat.cpp AssetBundleFileTable.cpp AssetsFileFormat.cpp AssetsFileReader.cpp AssetsFileTable.cpp AssetsReplacer.cpp AssetTypeClass.cpp BundleReplacer.cpp ClassDatabaseFile.cpp EngineVersion.cpp ResourceManagerFile.cpp dllmain.cpp stdafx.cpp TextureFileFormat.cpp)
target_include_directories (AssetsTools PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories (AssetsTools PRIVATE
	${LIBSQUISH_INCLUDE_DIR}
	${CRNLIB_INCLUDE_DIR}
)

target_compile_definitions(AssetsTools PRIVATE ASSETSTOOLS_EXPORTS)
set_target_properties(AssetsTools PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_link_libraries(AssetsTools PUBLIC libStringConverter)
target_link_libraries(AssetsTools PRIVATE
	libCompression
	libStringConverter
	CrnlibWrapLegacy CrnlibWrapUnity
	squish
	ispc_texcomp
	texgenpack
	${CRNLIB_LIBRARIES}
	astcenc-sse4.1-static
)

