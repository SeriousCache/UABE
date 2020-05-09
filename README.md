# Unity Assets Bundle Extractor
Unity .assets and AssetBundle editor

UABE is an editor for Unity 3.4+/4/5/2017/2018 .assets and AssetBundle files. It can create standalone mod installers from changes to .assets and/or bundles.
Type information extracted from Unity is used in order to generate text representations of various asset types. Custom MonoBehaviour types also are supported.
There are multiple plugins to convert Unity assets from/to common file formats :
- The Texture plugin can export and import .png and .tga files and decode&encode most texture formats used by Unity.
- The TextAsset plugin can export and import .txt files.
- The AudioClip plugin can export uncompressed .wav files from U5's AudioClip assets using FMOD, .m4a files from WebGL builds and Unity 4 sound files.
- The Mesh plugin can export .obj and .dae (Collada) files, also supporting rigged SkinnedMeshRenderers.
- The MovieTexture plugin can export and import .ogv (Ogg Theora) files.
- The TerrainData plugin can export and import .raw files readable by Unity.

You can find the main project page on https://community.7daystodie.com/topic/1871-unity-assets-bundle-extractor/.
