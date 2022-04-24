add_library (Mesh SHARED "Mesh.cpp" "MeshStructures.cpp" "AssimpMesh.cpp" Mesh.def)
target_include_directories (Mesh PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories (Mesh PRIVATE ${ASSIMP_INCLUDE_DIR})

set_target_properties(Mesh PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/Plugins"
	SUFFIX ".bep")
target_link_libraries(Mesh PRIVATE UABE_Generic AssetsTools libStringConverter assimp)
