Additions in 1.1 :
- Added batch processing, see Usage.txt for more information.
- Now the original bundle file gets closed properly when saving the modified one.
- Fixed the detection of a running file operation so the program can't longer crash when fastly opening another bundle file while a modified bundle file gets saved.

Additions in 1.2 :
- Added Unity 5 support (until 5.0.0f4; 5.0.0p4 doesn't work but I'll get a new version out soon)
- Added an Info button to view all assets in a bundle file.

1.2b fixes the TypeTree remover for some bundle files.

Additions in 1.3 :
- Added file format 0x0F support (so it works with all current Unity 5 versions).
- Added class databases for all Unity asset file formats (one for Unity 4 and one for Unity 5).
- Added a View Data button to view the data of the selected asset in a tree view (if the asset file format is known). Depending on the asset, it might take some time to load.
- Added an option to view the asset file list of .assets files and their dependencies (open mainData to get more exact file names). It might take some time to load.


Additions in 1.3b :
- Made the AssetBundleExtractor work without having a mainData file in a specific directory (some debugging stuff which I forgot to remove).
- Added asset dump export and raw export functionality.


Additions in 1.4 :
- Added asset importing functionality (from raw data or from asset dumps).
- Added asset adding and removing functionality. To add a new asset to the file table, add it to the ResourceManager (in mainData) or AssetBundle (in any bundle file) asset.
- Fixed many types in the Unity 5 type database.
- Added a type database editor.


Additions in 1.5a :
- Fixed a bug that broke the type tree when writing assets files
- Added a Plugin API and a Texture plugin
- Fixed a bug when reading aligned arrays that are no strings (only affects Dump Export)
- Cleaned up some of the code (so the functions that read an asset from bundle or .assets files use the same code for both)
- Added the file id inside the asset list to PPtrs in the asset Tree View
- Changed the AssetsBundle saving function to use the actual .assets file writing functions to prevent tree view remover issues


Additions in 1.6 :
- Cleaned up the asset tree view code.
- Added a view asset button to PPtrs so you can directly view referred assets in the same tree view.
- Fixed some asset writing bugs (caused by writing to the wrong file position).
- Added a TextAsset plugin.


Additions in 1.6b :
- Fixed raw asset exporting (the output file was previously opened in text mode)
- Updated classdata_0E.dat for Unity 5.1.1p3 (for previous Unity 5 versions, use the file from an earlier release)


Additions in 1.6c :
- Added a fallback to the file dialogs for Windows XP (even though I don't recommend using XP).


Additions in 1.7 :
- Fixed the TextAsset plugin.
- Added an AudioClip plugin which can export Unity 5 sounds to uncompressed 16-bit WAV files using FMOD. See https://7daystodie.com/forums/showthread.php?32600-Overriding-vanilla-files-(assets-)&p=314195&viewfull=1#post314195 if you want to import sounds.


Additions in 1.8 :
- Added the -keepnames switch for batch extracting, which is useful for making a webplayer game a standalone game
- Fixed the crash when trying to save a .assets if the target file is not writable
- Fixed the color channels of some raw texture formats
- Added Texture2D support for Unity 5.1 and newer, including support for reading/writing crunched textures
- Fixed Texture2D editing if the encoding and the texture itself wasn't changed (before it always reencoded the texture which only took time)
- Fixed exported texture's direction (converting Unity's 'bottom to top' to 'top to bottom')
- Fixed the metadata size field when saving .assets (at least I think it is correct now)
- Added colums to the asset list and sorting by these columns
- Added an asset search by name and a goto asset dialog
- Added support for asset batch exporting for all plugins (not batch importing)
- Added a Mesh to .obj plugin (export) and a MovieTexture plugin (import/export)
- Added support for Unity 4 AudioClip assets
- Fixed the asset dump for MonoBehaviours (which still doesn't include the script-specific data)
- Added a Unity 5.2.0f3 type database (which HAS to be used for new Texture2D assets)
- Fixed the Unity 4 type database
- Added .unity3d unpacking functionality (but not packing; if you want to use such a webplayer game, you can export it to a standalone one; I can give you instructions on how to do that)
- Fixed the Info button for bundles (it now shows the asset list from the selected assets' point of view)
- Improved the TreeView performance (quadratic vs. linear creation time)


Additions in 1.8b :
- Greatly improved the asset list creation performance
- Fixed a minor memory leak in Texture2D (when converting the texture data fails, it now also frees the memory)


Addition in 1.8c :
- Fixed the UV channels for some meshes and fixed the V orientation.

Additions in 1.8.1 :
- Added Unity 5.3 bundle support (LZ4 and uncompressed tested, LZMA not tested)
- Added a Unity 5.3.1p3 type database
- Added LZMA (default) or LZ4 compression for type databases
- Fixed bundle file operations after closing an info dialog
- Fixed negative signed integer values in the dump (int8, int16, int32)
- Fixed RGBA4444 Texture2D exporting
