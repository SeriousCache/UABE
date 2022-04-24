add_library (ModInstaller SHARED dllmain.cpp InstallDialog.cpp InstallerDataFormat.cpp MakeIconResource.cpp MakeInstaller.cpp ModInstaller.cpp stdafx.cpp ../UABE_Win32/FileDialog.cpp Dialogs.rc ModInstaller.manifest)
target_include_directories (ModInstaller PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(ModInstaller PRIVATE MODINSTALLER_EXPORTS)
set_target_properties(ModInstaller PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_link_libraries(ModInstaller PRIVATE AssetsTools_Static libCompression libStringConverter Comctl32.lib Shlwapi.lib)

