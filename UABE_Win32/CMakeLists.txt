add_library (UABE_Win32 SHARED AddAssetDialog.cpp AssetDependDialog.cpp AssetListDialog.cpp AssetViewModifyDialog.cpp  BatchImportDialog.cpp BundleDialog.cpp FileDialog.cpp MainWindow2.cpp ModInstallerEditor2.cpp ModPackageLoader.cpp MonoBehaviourManager.cpp ProgressDialog.cpp SelectClassDbDialog.cpp TypeDatabaseEditor.cpp TypeDbPackageEditor.cpp Win32AppContext.cpp Win32ModTreeDialogBase.cpp AssetBundleExtractor.rc "Win32BatchImportDesc.h" "api.h" "Win32BatchImportDesc.cpp"  "Win32TaskStatusTracker.cpp" "SplitterControlHandler.cpp" "Win32PluginManager.cpp")
target_include_directories (UABE_Win32 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${MCTRL_INCLUDE_DIR})

set_target_properties(UABE_Win32 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_link_libraries(UABE_Win32 PUBLIC UABE_Generic AssetsTools libStringConverter)
target_link_libraries(UABE_Win32 PRIVATE ModInstaller libCompression Comctl32.lib Shlwapi.lib mCtrl)
