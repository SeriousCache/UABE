find_package(fmod REQUIRED)

add_library (AudioClip SHARED "AudioClip.cpp" "wavfile.cpp" AudioClip.def)
target_include_directories (AudioClip PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories (AudioClip PRIVATE ${FMOD_INCLUDE_DIR})

set_target_properties(AudioClip PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/Plugins"
	SUFFIX ".bep")
target_link_libraries(AudioClip PRIVATE UABE_Generic AssetsTools libStringConverter ${FMOD_LIBRARIES})

set(AudioClip_MODULE_DEPENDENCY_PATHS ${FMOD_MODULES})

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(depfile ${AudioClip_MODULE_DEPENDENCY_PATHS})
	configure_file("${depfile}" "${CMAKE_BINARY_DIR}/bin" COPYONLY)
endforeach()
