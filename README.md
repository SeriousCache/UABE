# Asset Bundle Extractor
.assets and AssetBundle editor.  
Not affiliated with Unity Technologies.

UABE is an editor for 3.4+/4/5/2017-2021.3 .assets and AssetBundle files. It can create standalone mod installers from changes to .assets and/or bundles.

There are multiple plugins to convert assets from/to common file formats :
- The Texture plugin can export and import .png and .tga files (Texture2D only) and decode&encode most texture formats used by Unity.
- The TextAsset plugin can export and import .txt files.
- The AudioClip plugin can export uncompressed .wav files from Unity 5+ AudioClip assets using FMOD, .m4a files from WebGL builds and Unity 4 sound files.
- The Mesh plugin can export .obj and .dae (Collada) files, also supporting rigged SkinnedMeshRenderers.
- The Utility plugin can export and import byte arrays and resources (StreamingInfo, StreamedResource) within the View Data editor.

## Building
UABE can be built within Visual Studio (Community) 2022 using the Open Folder option (CMake).

The non-proprietary dependencies are downloaded and patched during CMake configuration.  
The proprietary dependencies are optional and can be disabled:
- FMOD: Remove the AudioClip plugin by removing the corresponding line in Plugins/CMakeLists.txt.
- PVRTexTool: Remove TexToolWrap by removing the corresponding line in CMakeLists.txt. This removes support for some texture formats used (mostly) for mobile games.

To embed the proprietary SDKs, set the PVRTexTool_ROOT and FMOD_ROOT CMake variables accordingly.  
The CMakeSettings.Example.json shows how a CMakeSettings.json for Visual Studio could look like.  
If the build process cannot find the SDKs, check if the cmake files in CMakeModules look in the correct subfolders. Also note that UABE is still using an old version of FMOD (with plans to substitute it entirely), so it may not work with recent versions.

### Portability Notes
- UABE uses plain Win32 for the GUI. The GUI portions are isolated to the UABE_Win32 module, some plugins and the mCtrl dependency. winelib could be an option for a Linux GUI port, however.
- Compilers other than MSVC++ are not tested with UABE and likely require some code changes.
- Uses C++20-feature std::format, which is not supported by gcc yet (as of writing this). [fmtlib](https://github.com/fmtlib/fmt) may be a quick drop-in replacement.

## License
UABE is licensed under the Eclipse Public License, v. 2.0 (EPL 2.0) license (see [Licenses/license.txt](Licenses/license.txt)).  
See [Readme.License.txt](Readme.License.txt) for more details, including a listing of dependencies and copyright notices.
