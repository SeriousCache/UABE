add_library (UABE_Generic SHARED AppContext.cpp AssetContainerList.cpp AssetIterator.cpp AsyncTask.cpp CreateEmptyValueField.cpp FileContext.cpp FileContextInfo.cpp FileModTree.cpp "IProgressIndicator.h" "IProgressIndicator.cpp" "api.h" "AssetPluginUtil.cpp" "IAssetBatchImportDesc.h" "TaskStatusTracker.cpp" "PluginManager.cpp")
target_include_directories (UABE_Generic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(UABE_Generic PRIVATE UABE_Generic_EXPORTS)
set_target_properties(UABE_Generic PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_link_libraries(UABE_Generic PUBLIC AssetsTools)
target_link_libraries(UABE_Generic PRIVATE ModInstaller libCompression libStringConverter jsmn)

