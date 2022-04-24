cmake_minimum_required (VERSION 3.18)
project(TypeTreeGenerator VERSION 1.0.0 LANGUAGES CSharp)

add_executable (TypeTreeGenerator Program.cs ClassDatabaseFile2.cs EngineVersion.cs HelperClass.cs Logger.cs TypeField.cs)
#Use a 'dummy' generator expression so VS does not add another subdirectory (e.g. Debug) below the given path.
set_target_properties (TypeTreeGenerator PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../Tools$<IF:1,,>")

set_target_properties (TypeTreeGenerator PROPERTIES VS_DOTNET_TARGET_FRAMEWORK_VERSION "v4.6.1")
set_target_properties (TypeTreeGenerator PROPERTIES WIN32_EXECUTABLE TRUE)
set (CMAKE_CSharp_FLAGS "/platform:x86")

set_target_properties (TypeTreeGenerator PROPERTIES VS_DOTNET_REFERENCE_MonoCecil "${CMAKE_CURRENT_LIST_DIR}/../Tools/Mono.Cecil.dll")
set_target_properties (TypeTreeGenerator PROPERTIES VS_DOTNET_REFERENCE_MonoCecilRocks "${CMAKE_CURRENT_LIST_DIR}/../Tools/Mono.Cecil.Rocks.dll")
set_target_properties (TypeTreeGenerator PROPERTIES VS_DOTNET_REFERENCES "System")
