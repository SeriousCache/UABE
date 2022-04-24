using System;
using System.Collections.Generic;
using Mono.Cecil;
using Mono.Cecil.Rocks;

namespace TypeTreeGenerator
{
	public class TypeField
	{
		class TypeInfo
		{
			public string fullName;
			public TypeReference cecilType;
			public TypeField _base;

			public List<TypeField> _instances;
			public bool baseComplete;
			public TypeInfo(TypeReference tref)
			{
				baseComplete = false;
				this.fullName = tref.FullName;
				this._base = null;
				this.cecilType = tref;
				this._instances = new List<TypeField>();
			}
		}
		static List<TypeInfo> resolvedTypes = new List<TypeInfo>();
		static TypeInfo FindTypeInDatabase(string fullName)
		{
			foreach (TypeInfo curType in resolvedTypes)
			{
				if (curType.fullName.Equals(fullName))
					return curType;
			}
			return null;
		}
		static TypeInfo FindTypeInDatabase(TypeReference tref)
		{
			return FindTypeInDatabase(tref.FullName);
		}
		static TypeInfo AddToDatabase(TypeInfo type)
		{
			if (type.cecilType.Resolve().IsEnum)
				return null;
			switch (type.fullName)
			{
				case "System.Boolean":
				case "System.Byte":
				case "System.Char":
				case "System.SByte":
				case "System.Int16":
				case "System.UInt16":
				case "System.Int32":
				case "System.UInt32":
				case "System.Int64":
				case "System.UInt64":
				case "System.Single":
				case "System.Double":
				case "System.String":
				case "System.Collections.Generic.List":
				case "System.Collections.Generic.HashSet":
				case "System.Collections.Generic.Dictionary":
				case "System.Collections.Hashtable":
					return null;
			}
			TypeInfo ret = FindTypeInDatabase(type.fullName);
			if (ret == null)
			{
				ret = type;
				resolvedTypes.Add(ret);
			}
			return ret;
		}
		public List<TypeField> children;
        /*public enum ValueType
		{
			_None=0,
			_Bool,
			_Int8,
			_UInt8,
			_Int16,
			_UInt16,
			_Int32,
			_UInt32,
			_Int64,
			_UInt64,
			_Float,
			_Double,
			_String,
			_Array
		}
		ValueType valueType;*/
        public bool isBuiltinType = false;

		public bool isArray = false;
		//for MonoBehaviors, types that are not 4 or 8 bytes long (bytes,bools,shorts,strings,...) always are aligned (8 1-byte variables take 8*4 bytes)
        //flag 0x4000
		public bool hasAlignment = false;
        public int version = 1;
		public TypeField _base;
		public string type;
        public string monoType; public string baseModuleName;
		public string name;
		public int size
		{
			get
			{ 
				if (children.Count == 0)
				{
					switch (this.type)
					{
						case "bool":
						case "UInt8":
						case "char":
						case "SInt8":
							return 1;
						case "SInt16":
						case "UInt16":
							return 2;
						case "int":
						case "unsigned int":
						case "float":
							return 4;
						case "double":
						case "SInt64":
						case "UInt64":
							return 8;
						default:
							return -1;
					}
				}
				else if (this.isArray)
					return -1;
				else
				{
					int ret = 0;
					foreach (TypeField child in children)
					{
						int childSize = child.size; //Don't include alignment.
						if (childSize == -1)
							return -1;
						ret += childSize;
					}
					return ret;
				}
			}
		}
		public TypeField(TypeField[] children, string type, string name)
		{
			_base = this;
			this.children = new List<TypeField>(children);
			this.type = type;
			this.name = name;
		}
		bool TestEquation_(TypeField other, List<TypeField> tested, bool testName)
		{
			if ((other.isArray == this.isArray) && 
				(other.hasAlignment == this.hasAlignment) && 
				(other.children.Count == this.children.Count) && 
				(other.type.Equals(this.type)) && 
				(testName ? other.name.Equals(this.name) : true))
			{
				if (!tested.Contains(this))
				{
					for (int i = 0; i < this.children.Count; i++)
						if (!this.children[i].TestEquation_(other.children[i], new List<TypeField>(tested), true))
                            return false;
                    tested.Add(this);
				}
				return true;
			}
			return false;
		}
		public bool TestEquation(TypeField other, bool testName = true)
		{
			if ((other.isArray == this.isArray) && 
				(other.hasAlignment == this.hasAlignment) && 
				(other.children.Count == this.children.Count) && 
				(other.type.Equals(this.type)) && 
				(testName ? other.name.Equals(this.name) : true))
			{
				List<TypeField> tparents = new List<TypeField>();
				tparents.Add(other);
				for (int i = 0; i < this.children.Count; i++)
					if (!this.children[i].TestEquation_(other.children[i], new List<TypeField>(tparents), true))
						return false;
				return true;
			}
			return false;
		}
		private void TestSelfContain(TypeReference self, TypeReference curType, List<TypeField> parents)
		{
			bool isObject = false;
			TypeDefinition curBase = curType.Resolve();
			while (curBase != null)
			{
				if (curBase.FullName.Equals("UnityEngine.Object"))
				{
					isObject = true;
					break;
				}
				TypeReference baseRef = curBase.BaseType;
				if (baseRef == null)
					break;
				curBase = baseRef.Resolve();
			}
			if (!isObject)
			{
				if (HelperClass.GetFullName(curType).Equals(HelperClass.GetFullName(self)))
					throw new ArgumentException("The class contains itself! (Unity itself allows this but stops serializing at a certain depth)");
				/*foreach (TypeField parent in parents)
				{
					if (parent.type.Equals(curType.Name))
						throw new ArgumentException("A class contains one of its parent classes!");
				}*/
			}
		}

        //https://docs.unity3d.com/560/Documentation/Manual/script-Serialization.html
        private static readonly string[] SerializableInternalUnityTypes = {
            "UnityEngine.Vector2",
            "UnityEngine.Vector3",
            "UnityEngine.Vector4",
            "UnityEngine.Rect",
            "UnityEngine.Quaternion",
            "UnityEngine.Matrix4x4",
            "UnityEngine.Color",
            "UnityEngine.Color32",
            "UnityEngine.LayerMask",
            "UnityEngine.AnimationCurve",
            "UnityEngine.Gradient",
            "UnityEngine.RectOffset",
            "UnityEngine.GUIStyle",
        };
        private bool IsSerializable(TypeReference typeRef)
        {
            TypeDefinition _typeDef = typeRef.Resolve();
            if (typeRef.IsArray)
            {
                return IsSerializable(((ArrayType)typeRef).ElementType);
            }
            if (typeRef.FullName.StartsWith("System.Collections.Generic.List"))
            {
                if (typeRef is GenericInstanceType)
                {
                    TypeReference nestedType = ((GenericInstanceType)typeRef).GenericArguments[0];
                    //TODO: nestedType should not be inside mscorlib
                    return IsSerializable(nestedType);
                }
                else
                    return false;
            }
            if (typeRef.Name.EndsWith("Dictionary`2"))
                return false;
            if (_typeDef != null)
            {
                if (_typeDef.IsInterface || _typeDef.IsAbstract)
                    return false;
                if (_typeDef.IsEnum || _typeDef.IsSerializable || HelperClass.BaseTypeComparer("UnityEngine.Object").Execute(_typeDef))
                    return true;
            }
            if (typeRef.Namespace.StartsWith("UnityEngine"))
            {
                foreach (string builtinType in SerializableInternalUnityTypes)
                {
                    if (typeRef.FullName.Equals(builtinType))
                        return true;
                }
            }
            return false;
        }
        private void EmitArrayType(TypeReference type, TypeReference elementType, EngineVersion engineVersion, List<TypeField> parents)
        {
            TestSelfContain(type, elementType, parents);
            TypeField itemCount = new TypeField(new TypeField[0], "int", "size");
			
            TypeInfo typeInfo = null;
            TypeField data = null;
            bool isObject = HelperClass.BaseTypeComparer("UnityEngine.Object").Execute(elementType.Resolve());
            if (!isObject)
            {
                typeInfo = FindTypeInDatabase(elementType);
                if (typeInfo == null)
                {
                    TypeField baseField = new TypeField(elementType, String.Empty, engineVersion, new List<TypeField>(parents));
                    typeInfo = new TypeInfo(elementType);
                    typeInfo._base = baseField;
                    typeInfo = AddToDatabase(typeInfo);
                }
            }
            if (typeInfo != null)
            {
                data = new TypeField(typeInfo._base.children.ToArray(), typeInfo._base.type/*"Generic Mono"*/, "data");
                data.children = typeInfo._base.children;
                data._base = typeInfo._base;
            }
            else //the type either is a value type or a UnityEngine.Object
            {
                data = new TypeField(elementType, "data", engineVersion, new List<TypeField>(parents));
                if (data.hasAlignment)
                {
                    int elementSize = data.size;
                    if (elementSize > 0 && elementSize < 4) //For byte/word arrays, align the array instead of each element.
                    {
                        data.hasAlignment = false;
                        this.hasAlignment = true;
                    }
                }
            }
            this.type = data.isBuiltinType ? "vector" : data.type; //Seems strange, but Unity does name the array field type that way.
            TypeField array = new TypeField(new TypeField[] { itemCount, data }, "Array", "Array");
            array.isArray = true;
            if (data.size != -1 || data.children.Count != 0)
            {
                children.Add(array);
            }
        }
		public TypeField(TypeReference type, string name, EngineVersion engineVersion, List<TypeField> parents = null, bool isBase = false)
		{
            bool pathIdIsInt64 = (engineVersion.year >= 5);
            this.monoType = HelperClass.GetFullName(type);
            this.type = type.Name;
            this.baseModuleName = type.Module.Name;
			_base = this;
			TypeInfo ownTypeInfo = null, _tmpOwnTypeInfo = null;
			if ((_tmpOwnTypeInfo = FindTypeInDatabase(type)) == null)
			{
				ownTypeInfo = AddToDatabase(new TypeInfo(type));
				if (ownTypeInfo != null)
				{
					ownTypeInfo._base = this;
				}
			}
			else if (!_tmpOwnTypeInfo.baseComplete)
				_tmpOwnTypeInfo._instances.Add(this);
			if (parents == null)
				parents = new List<TypeField>();
			parents.Add(this);
			
			this.children = new List<TypeField>();
			
			TypeDefinition tDef = type.Resolve();
			this.name = name;
			
			if (type.IsArray)
			{
                EmitArrayType(type, ((ArrayType)type).ElementType, engineVersion, parents);
			}
			else if (type.FullName.StartsWith("System.Collections.Generic.List")/* || type.FullName.StartsWith("System.Collections.Generic.HashSet")*/)
			{
				if (type is GenericInstanceType)
                {
                    GenericInstanceType genType = (GenericInstanceType)type;
                    EmitArrayType(type, genType.GenericArguments[0], engineVersion, parents);
				}
			}
			else
			{
				if (!tDef.IsEnum)
                {
                    bool isObject = HelperClass.BaseTypeComparer("UnityEngine.Object").Execute(tDef);
                    if (!isBase && isObject)
                    {
                        isBuiltinType = true;
                        TypeField fileId = new TypeField(new TypeField[0], "int", "m_FileID");
						TypeField pathId = new TypeField(new TypeField[0], pathIdIsInt64 ? "SInt64" : "int", "m_PathID");
						children.Add(fileId);
						children.Add(pathId);
						this.type = "PPtr<$" + tDef.Name + ">";
						if (_tmpOwnTypeInfo != null && !_tmpOwnTypeInfo.baseComplete)
							_tmpOwnTypeInfo._instances.Remove(this);
					}
					else
					{
						string valueTypeName = "";
                        hasAlignment = false; //Only set for simple value types; in Arrays of UInt8, only the UInt8 data has alignment set.
						switch (tDef.FullName)
						{
                            case "System.Boolean":
                            case "System.Byte":
                                isBuiltinType = true;
								hasAlignment = true;
								valueTypeName = "UInt8";
								break;
							case "System.SByte":
                                isBuiltinType = true;
                                hasAlignment = true;
								valueTypeName = "SInt8";
								break;
							case "System.Int16":
                                isBuiltinType = true;
                                hasAlignment = true;
								valueTypeName = "SInt16";
                                break;
                            case "System.Char":
							case "System.UInt16":
                                isBuiltinType = true;
                                hasAlignment = true;
								valueTypeName = "UInt16";
								break;
							case "System.Int32":
                                isBuiltinType = true;
                                hasAlignment = false;
								valueTypeName = "int";
								break;
							case "System.UInt32":
                                isBuiltinType = true;
                                hasAlignment = false;
								valueTypeName = "unsigned int";
								break;
							case "System.Int64":
                                isBuiltinType = true;
                                hasAlignment = false;
								valueTypeName = "SInt64";
								break;
							case "System.UInt64":
                                isBuiltinType = true;
                                hasAlignment = false;
								valueTypeName = "UInt64";
								break;
							case "System.Single":
                                isBuiltinType = true;
                                hasAlignment = false;
								valueTypeName = "float";
								break;
							case "System.Double":
                                isBuiltinType = true;
                                hasAlignment = false;
								valueTypeName = "double";
                                break;
                            case "UnityEngine.AnimationCurve":
                                {
                                    isBuiltinType = true;
                                    hasAlignment = false;
                                    valueTypeName = "AnimationCurve";
									version=1; //2 since 5.5.*
									int keyframe_version = 1; //2 since 5.5.*, 3 since 2018.1.*
                                    if (engineVersion.year > 5 || (engineVersion.year == 5 && engineVersion.release >= 5))
                                    {
                                        version = 2;
                                        keyframe_version = 2;
                                    }
                                    if (engineVersion.year >= 2018)
                                    {
                                        keyframe_version = 3;
                                    }
                                    TypeField data;
									switch (keyframe_version)
									{
                                    default:
									case 3:
										 data = new TypeField(new TypeField[]
											{
												new TypeField(new TypeField[0], "float", "time"),
												new TypeField(new TypeField[0], "float", "value"),
												new TypeField(new TypeField[0], "float", "inSlope"),
												new TypeField(new TypeField[0], "float", "outSlope"),
												new TypeField(new TypeField[0], "int", "weightedMode"),
												new TypeField(new TypeField[0], "float", "inWeight"),
												new TypeField(new TypeField[0], "float", "outWeight"),
											}, "Keyframe", "data");
										data.version = 3;
										break;
									case 2: //Seems to be identical to v1?
										 data = new TypeField(new TypeField[]
											{
												new TypeField(new TypeField[0], "float", "time"),
												new TypeField(new TypeField[0], "float", "value"),
												new TypeField(new TypeField[0], "float", "inSlope"),
												new TypeField(new TypeField[0], "float", "outSlope"),
											}, "Keyframe", "data");
										data.version = 2;
										break;
									case 1:
										 data = new TypeField(new TypeField[]
											{
												new TypeField(new TypeField[0], "float", "time"),
												new TypeField(new TypeField[0], "float", "value"),
												new TypeField(new TypeField[0], "float", "inSlope"),
												new TypeField(new TypeField[0], "float", "outSlope"),
											}, "Keyframe", "data");
										data.version = 1;
										break;
									}
								    TypeField size = new TypeField(new TypeField[0], "int", "size");
								    TypeField array = new TypeField(new TypeField[]{size,data}, "Array", "Array");
								    array.isArray = true;
								    TypeField curve = new TypeField(new TypeField[]{array}, "vector", "m_Curve");
                                    children.Add(curve);

								    TypeField preInfinity = new TypeField(new TypeField[0], "int", "m_PreInfinity");
                                    children.Add(preInfinity);

                                    TypeField postInfinity = new TypeField(new TypeField[0], "int", "m_PostInfinity");
                                    children.Add(postInfinity);

									if (version >= 2 || (version == 1 && engineVersion.year == 5 && engineVersion.release >= 3)) 
									{
                                        //Version 1 AnimationCurves from 5.3.* also have this field.
                                        TypeField rotationOrder = new TypeField(new TypeField[0], "int", "m_RotationOrder");
										children.Add(rotationOrder);
									}
                                }
                                break;
                            case "UnityEngine.Vector2":
                                {
                                    isBuiltinType = true;
                                    hasAlignment = false;
                                    valueTypeName = "Vector2f";
                                    TypeField x = new TypeField(new TypeField[0], "float", "x");
                                    children.Add(x);
                                    TypeField y = new TypeField(new TypeField[0], "float", "y");
                                    children.Add(y);
                                }
                                break;
                            case "UnityEngine.Vector3":
                                {
                                    isBuiltinType = true;
                                    hasAlignment = false;
                                    valueTypeName = "Vector3f";
                                    TypeField x = new TypeField(new TypeField[0], "float", "x");
                                    children.Add(x);
                                    TypeField y = new TypeField(new TypeField[0], "float", "y");
                                    children.Add(y);
                                    TypeField z = new TypeField(new TypeField[0], "float", "y");
                                    children.Add(z);
                                }
                                break;
                            case "UnityEngine.Vector4":
                                {
                                    isBuiltinType = true;
                                    hasAlignment = false;
                                    valueTypeName = "Vector4f";
                                    TypeField x = new TypeField(new TypeField[0], "float", "x");
                                    children.Add(x);
                                    TypeField y = new TypeField(new TypeField[0], "float", "y");
                                    children.Add(y);
                                    TypeField z = new TypeField(new TypeField[0], "float", "z");
                                    children.Add(z);
                                    TypeField w = new TypeField(new TypeField[0], "float", "w");
                                    children.Add(w);
                                }
                                break;
                            case "UnityEngine.Rect":
                                {
                                    isBuiltinType = true;
                                    hasAlignment = false;
									version = 2; //What is version 1?
                                    valueTypeName = "Rectf";
                                    TypeField x = new TypeField(new TypeField[0], "float", "x");
                                    children.Add(x);
                                    TypeField y = new TypeField(new TypeField[0], "float", "y");
                                    children.Add(y);
                                    TypeField w = new TypeField(new TypeField[0], "float", "width");
                                    children.Add(w);
                                    TypeField h = new TypeField(new TypeField[0], "float", "height");
                                    children.Add(h);
                                }
                                break;
                            case "UnityEngine.RectOffset":
                                {
                                    isBuiltinType = true;
                                    hasAlignment = false;
                                    valueTypeName = "RectOffset";
                                    TypeField l = new TypeField(new TypeField[0], "int", "m_Left");
                                    children.Add(l);
                                    TypeField r = new TypeField(new TypeField[0], "int", "m_Right");
                                    children.Add(r);
                                    TypeField t = new TypeField(new TypeField[0], "int", "m_Top");
                                    children.Add(t);
                                    TypeField b = new TypeField(new TypeField[0], "int", "m_Bottom");
                                    children.Add(b);
                                }
                                break;
                            case "UnityEngine.Quaternion":
                                {
                                    isBuiltinType = true;
                                    hasAlignment = false;
                                    valueTypeName = "Quaternionf";
                                    TypeField x = new TypeField(new TypeField[0], "float", "x");
                                    children.Add(x);
                                    TypeField y = new TypeField(new TypeField[0], "float", "y");
                                    children.Add(y);
                                    TypeField z = new TypeField(new TypeField[0], "float", "z");
                                    children.Add(z);
                                    TypeField w = new TypeField(new TypeField[0], "float", "w");
                                    children.Add(w);
                                }
                                break;
                            case "UnityEngine.Matrix4x4":
                                {
                                    isBuiltinType = true;
                                    hasAlignment = false;
                                    valueTypeName = "Matrix4x4f";
                                    for (int a = 0; a <= 3; a++)
                                    {
                                        for (int b = 0; b <= 3; b++)
                                        {
                                            children.Add(new TypeField(new TypeField[0], "float", "e"+a+b));
                                        }
                                    }
                                }
                                break;
                            case "UnityEngine.Color":
                                {
                                    isBuiltinType = true;
                                    hasAlignment = false;
                                    valueTypeName = "ColorRGBA";
                                    TypeField r = new TypeField(new TypeField[0], "float", "r");
                                    children.Add(r);
                                    TypeField g = new TypeField(new TypeField[0], "float", "g");
                                    children.Add(g);
                                    TypeField b = new TypeField(new TypeField[0], "float", "b");
                                    children.Add(b);
                                    TypeField a = new TypeField(new TypeField[0], "float", "a");
                                    children.Add(a);
                                }
                                break;
                            case "UnityEngine.Color32":
                                {
                                    isBuiltinType = true;
                                    hasAlignment = false;
                                    valueTypeName = "ColorRGBA"; //Same type name as the float ColorRGBA.
                                    TypeField rgba = new TypeField(new TypeField[0], "unsigned int", "rgba");
                                    children.Add(rgba);
                                }
                                break;
                            case "UnityEngine.LayerMask": //Transfer_Blittable_SingleValueField<class GenerateTypeTreeTransfer,struct BitField>
                                {
                                    isBuiltinType = true;
                                    hasAlignment = false;
									version = 2; //What is 1?
                                    valueTypeName = "BitField";
                                    TypeField bits = new TypeField(new TypeField[0], "unsigned int", "m_Bits");
                                    children.Add(bits);
                                }
                                break;
                            case "UnityEngine.GUIStyle":
                                {
                                    isBuiltinType = true;
                                    hasAlignment = false;
                                    valueTypeName = "GUIStyle";
                                    //string m_Name
                                    {
                                        TypeField size = new TypeField(new TypeField[0], "int", "size");
                                        TypeField data = new TypeField(new TypeField[0], "char", "data");
                                        TypeField array = new TypeField(new TypeField[] { size, data }, "Array", "Array");
                                        array.isArray = true; array.hasAlignment = true;
                                        TypeField nameString = new TypeField(new TypeField[] { array }, "string", "m_Name");
                                        nameString.hasAlignment = true; //Unlike string fields in usual scripts.
                                        children.Add(nameString);
                                    }
                                    //GUIStyleState
                                    {
                                        TypeField fileId = new TypeField(new TypeField[0], "int", "m_FileID");
                                        TypeField pathId = new TypeField(new TypeField[0], pathIdIsInt64 ? "SInt64" : "int", "m_PathID");
                                        TypeField background = new TypeField(new TypeField[] { fileId, pathId }, "PPtr<Texture2D>", "m_Background");

                                        //m_ScaledBackgrounds is located in the type information of MonoBehaviours in bundled .assets
                                        //but it's not actually serialized (at least in 5.6.0f3);
                                        //while leaving it out clearly solves reading/writing dumps, it still breaks the type hash.
                                        //TODO : Further check this and consider adding a "ghost field" flag to type information so such fields can be skipped for (de)serializing.
                                        /*TypeField size = new TypeField(new TypeField[0], "int", "size");
                                        TypeField data = new TypeField(new TypeField[] { fileId, pathId }, "PPtr<Texture2D>", "data");
                                        TypeField array = new TypeField(new TypeField[] { size, data }, "Array", "Array");
                                        array.isArray = true;
                                        TypeField scaledBackgrounds = new TypeField(new TypeField[] { array }, "vector", "m_ScaledBackgrounds");*/

                                        TypeField r = new TypeField(new TypeField[0], "float", "r");
                                        TypeField g = new TypeField(new TypeField[0], "float", "g");
                                        TypeField b = new TypeField(new TypeField[0], "float", "b");
                                        TypeField a = new TypeField(new TypeField[0], "float", "a");
                                        TypeField textColor = new TypeField(new TypeField[] { r, g, b, a }, "ColorRGBA", "m_TextColor");

                                        TypeField normal = new TypeField(new TypeField[] { background/*, scaledBackgrounds*/, textColor }, "GUIStyleState", "m_Normal");
                                        children.Add(normal);
                                        TypeField hover = new TypeField(new TypeField[] { background/*, scaledBackgrounds*/, textColor }, "GUIStyleState", "m_Hover");
                                        children.Add(hover);
                                        TypeField active = new TypeField(new TypeField[] { background/*, scaledBackgrounds*/, textColor }, "GUIStyleState", "m_Active");
                                        children.Add(active);
                                        TypeField focused = new TypeField(new TypeField[] { background/*, scaledBackgrounds*/, textColor }, "GUIStyleState", "m_Focused");
                                        children.Add(focused);
                                        TypeField onNormal = new TypeField(new TypeField[] { background/*, scaledBackgrounds*/, textColor }, "GUIStyleState", "m_OnNormal");
                                        children.Add(onNormal);
                                        TypeField onHover = new TypeField(new TypeField[] { background/*, scaledBackgrounds*/, textColor }, "GUIStyleState", "m_OnHover");
                                        children.Add(onHover);
                                        TypeField onActive = new TypeField(new TypeField[] { background/*, scaledBackgrounds*/, textColor }, "GUIStyleState", "m_OnActive");
                                        children.Add(onActive);
                                        TypeField onFocused = new TypeField(new TypeField[] { background/*, scaledBackgrounds*/, textColor }, "GUIStyleState", "m_OnFocused");
                                        children.Add(onFocused);
                                    }
                                    //RectOffset
                                    {
                                        TypeField l = new TypeField(new TypeField[0], "int", "m_Left");
                                        TypeField r = new TypeField(new TypeField[0], "int", "m_Right");
                                        TypeField t = new TypeField(new TypeField[0], "int", "m_Top");
                                        TypeField b = new TypeField(new TypeField[0], "int", "m_Bottom");

                                        TypeField border = new TypeField(new TypeField[] { l, r, t, b }, "RectOffset", "m_Border");
                                        children.Add(border);
                                        TypeField margin = new TypeField(new TypeField[] { l, r, t, b }, "RectOffset", "m_Margin");
                                        children.Add(margin);
                                        TypeField padding = new TypeField(new TypeField[] { l, r, t, b }, "RectOffset", "m_Padding");
                                        children.Add(padding);
                                        TypeField overflow = new TypeField(new TypeField[] { l, r, t, b }, "RectOffset", "m_Overflow");
                                        children.Add(overflow);
                                    }
                                    //PPtr<Font> m_Font
                                    {
                                        TypeField fileId = new TypeField(new TypeField[0], "int", "m_FileID");
                                        TypeField pathId = new TypeField(new TypeField[0], pathIdIsInt64 ? "SInt64" : "int", "m_PathID");
                                        TypeField font = new TypeField(new TypeField[] { fileId, pathId }, "PPtr<Font>", "m_Font");
                                        children.Add(font);
                                    }
                                    TypeField fontSize = new TypeField(new TypeField[0], "int", "m_FontSize");
                                    children.Add(fontSize);
                                    TypeField fontStyle = new TypeField(new TypeField[0], "int", "m_FontStyle");
                                    children.Add(fontStyle);
                                    TypeField alignment = new TypeField(new TypeField[0], "int", "m_Alignment");
                                    children.Add(alignment);
                                    TypeField wordWrap = new TypeField(new TypeField[0], "bool", "m_WordWrap");
                                    children.Add(wordWrap);
                                    TypeField richText = new TypeField(new TypeField[0], "bool", "m_RichText");
                                    richText.hasAlignment = true;
                                    children.Add(richText);
                                    TypeField textClipping = new TypeField(new TypeField[0], "int", "m_TextClipping");
                                    children.Add(textClipping);
                                    TypeField imagePosition = new TypeField(new TypeField[0], "int", "m_ImagePosition");
                                    children.Add(imagePosition);
                                    //Vector2f m_ContentOffset
                                    {
                                        TypeField x = new TypeField(new TypeField[0], "float", "x");
                                        TypeField y = new TypeField(new TypeField[0], "float", "y");
                                        TypeField contentOffset = new TypeField(new TypeField[] { x, y }, "Vector2f", "m_ContentOffset");
                                        children.Add(contentOffset);
                                    }
                                    TypeField fixedWidth = new TypeField(new TypeField[0], "float", "m_FixedWidth");
                                    children.Add(fixedWidth);
                                    TypeField fixedHeight = new TypeField(new TypeField[0], "float", "m_FixedHeight");
                                    children.Add(fixedHeight);
                                    TypeField stretchWidth = new TypeField(new TypeField[0], "bool", "m_StretchWidth");
                                    children.Add(stretchWidth);
                                    TypeField stretchHeight = new TypeField(new TypeField[0], "bool", "m_StretchHeight");
                                    stretchHeight.hasAlignment = true;
                                    children.Add(stretchHeight);
                                }
                                break;
							case "System.String":
                                {
                                    isBuiltinType = true;
                                    hasAlignment = false;
									TypeField size = new TypeField(new TypeField[0], "int", "size");
									TypeField data = new TypeField(new TypeField[0], "char", "data");
									TypeField array = new TypeField(new TypeField[]{size,data}, "Array", "Array");
									array.isArray = true; array.hasAlignment = true;
									children.Add(array);
									valueTypeName = "string";
								}
								break;
                            case "UnityEngine.Gradient":
                                {
                                    isBuiltinType = true;
                                    /* version 1 :
                                     * only 8x ColorRGBA32 key0-key7 (each has unsigned int rgba)
                                     * */
                                    hasAlignment = false;
                                    valueTypeName = "Gradient";
                                    version = 2;
                                    TypeField r = new TypeField(new TypeField[0], "float", "r");
                                    TypeField g = new TypeField(new TypeField[0], "float", "g");
                                    TypeField b = new TypeField(new TypeField[0], "float", "b");
                                    TypeField a = new TypeField(new TypeField[0], "float", "a");
                                    string[] colorNames = { "key0", "key1", "key2", "key3", "key4", "key5", "key6", "key7" };
                                    for (int i = 0; i < colorNames.Length; i++)
                                    {
                                        TypeField key = new TypeField(new TypeField[] { r, g, b, a }, "ColorRGBA", colorNames[i]);
                                        children.Add(key);
                                    }
                                    string[] ctimeNames = { "ctime0", "ctime1", "ctime2", "ctime3", "ctime4", "ctime5", "ctime6", "ctime7" };
                                    for (int i = 0; i < ctimeNames.Length; i++)
                                    {
                                        TypeField ctime = new TypeField(new TypeField[0], "UInt16", ctimeNames[i]);
                                        children.Add(ctime);
                                    }
                                    string[] atimeNames = { "atime0", "atime1", "atime2", "atime3", "atime4", "atime5", "atime6", "atime7" };
                                    for (int i = 0; i < atimeNames.Length; i++)
                                    {
                                        TypeField atime = new TypeField(new TypeField[0], "UInt16", atimeNames[i]);
                                        children.Add(atime);
                                    }
                                    TypeField mode = new TypeField(new TypeField[0], "int", "m_Mode");
                                    children.Add(mode);
                                    TypeField colorKeyNum = new TypeField(new TypeField[0], "UInt8", "m_NumColorKeys");
                                    children.Add(colorKeyNum);
                                    TypeField alphaKeyNum = new TypeField(new TypeField[0], "UInt8", "m_NumAlphaKeys");
                                    children.Add(alphaKeyNum);

                                    break;
                                }
                            /*case "UnityEngine.Bounds":
                                {
									hasAlignment = false;
                                    TypeField centerX = new TypeField(new TypeField[0], "float", "x");
                                    TypeField centerY = new TypeField(new TypeField[0], "float", "y");
                                    TypeField centerZ = new TypeField(new TypeField[0], "float", "z");
                                    TypeField center = new TypeField(new TypeField[]{centerX,centerY,centerZ}, "Vector3f", "m_Center");
                                    children.Add(center);
                                    TypeField extentsX = new TypeField(new TypeField[0], "float", "x");
                                    TypeField extentsY = new TypeField(new TypeField[0], "float", "y");
                                    TypeField extentsZ = new TypeField(new TypeField[0], "float", "z");
                                    TypeField extents = new TypeField(new TypeField[]{centerX,centerY,centerZ}, "Vector3f", "m_Extents");
                                    children.Add(extents);
									valueTypeName = "Bounds";
                                }
                                break;*/
                            default:
                                valueTypeName = this.type;
                                //Recursively resolve the base fields (i.e. superclass fields).
                                {
                                    TypeReference curBase = tDef.BaseType;
                                    if (curBase != null 
                                        && !curBase.FullName.Equals("UnityEngine.ScriptableObject") 
                                        && !curBase.FullName.Equals("UnityEngine.MonoBehaviour") 
                                        && !curBase.FullName.Equals("UnityEngine.Object")
                                        && !curBase.FullName.Equals("System.Object")
                                        && !curBase.Name.Equals("Dictionary`2")) //Scripting::IsSystemCollectionsGenericDictionary
                                    {
                                        TypeReference fullBaseTypeRef = HelperClass.ResolveGenericTypeRefs(type, curBase);

                                        //TestSelfContain(type, curBase, parents);
                                        TypeField tempField = new TypeField(fullBaseTypeRef, "TempBase", engineVersion, new List<TypeField>(parents), true);
                                        children.InsertRange(0, tempField.children);
                                    }
                                }
								
                                //SerializePrivateVariables is obsolete but still checked for in Unity (as of 5.6.0f3). 
                                bool doSerializePrivateVariables = false;//tDef.IsValueType;
                                foreach (CustomAttribute curAttribute in tDef.CustomAttributes)
                                {
                                    if (curAttribute.AttributeType.FullName.Equals("UnityEngine.SerializePrivateVariables"))
                                    {
                                        doSerializePrivateVariables = true;
                                        break;
                                    }
                                }
                                foreach (FieldDefinition field in tDef.Fields)
                                {
                                    FieldDefinition newField = field;
                                    TypeReference fieldTypeRef = HelperClass.ResolveGenericTypeRefs(type, newField.FieldType);
                                    TypeDefinition fieldType = fieldTypeRef.Resolve();
									
                                    if (fieldType == null)
                                    {
                                        throw new ArgumentException("Unable to resolve the field type!");
                                    }
                                    bool forceFieldSerialize = false;
                                    foreach (CustomAttribute attr in field.CustomAttributes)
                                    {
                                        if (attr.AttributeType.FullName.Equals("UnityEngine.SerializeField"))
                                        {
                                            forceFieldSerialize = true;
                                            break;
                                        }
                                    }
                                    isObject = HelperClass.BaseTypeComparer("UnityEngine.Object").Execute(fieldType);
                                    if ((!field.IsPublic && !forceFieldSerialize && !doSerializePrivateVariables))
                                        continue;
                                    if (field.IsNotSerialized)
                                        continue;
                                    if (HelperClass.BaseTypeComparer("System.MulticastDelegate").Execute(fieldType))
                                        continue;

									string fieldName = newField.Name;
									if (!field.IsNotSerialized && !field.IsStatic && !field.IsInitOnly && IsSerializable(fieldTypeRef))
									{
										TypeInfo typeInfo = null;
										if (!isObject)
										{
											typeInfo = FindTypeInDatabase(newField.FieldType);
											if (typeInfo == null)
											{
												TypeField baseField = new TypeField(fieldTypeRef, String.Empty, engineVersion);
												typeInfo = new TypeInfo(fieldTypeRef);
												typeInfo._base = baseField;
												typeInfo = AddToDatabase(typeInfo);
											}
										}
										TypeReference[] gargs = new TypeReference[0];
										if (type is GenericInstanceType)
										{
											gargs = ((GenericInstanceType)type).GenericArguments.ToArray();
										}
										
										TypeField newChildField;
										if (typeInfo != null)
										{
											newChildField = new TypeField(typeInfo._base.children.ToArray(), typeInfo._base.type, fieldName);
											newChildField.hasAlignment = typeInfo._base.hasAlignment;
											newChildField.isArray = typeInfo._base.isArray;
											newChildField._base = typeInfo._base;
										}
										else
											newChildField = new TypeField(fieldTypeRef, fieldName, engineVersion, new List<TypeField>(parents));
										if (newChildField.size != -1 || newChildField.children.Count != 0)
										{
											children.Add(newChildField);
										}
									}
                                }
                                break;
						}
						this.type = valueTypeName;
					}
				}
				else
				{
					hasAlignment = false;
					this.type = "int";
				}
			}
			if (ownTypeInfo != null)
			{
				foreach (TypeField instance in ownTypeInfo._instances)
				{
					instance.children.Clear();
					for (int i = 0; i < this.children.Count; i++)
						instance.children.Add(this.children[i]);
				}
				ownTypeInfo._instances.Clear();
				ownTypeInfo.baseComplete = true;
			}
		}
		public TypeField(FieldDefinition field, EngineVersion engineVersion) : this(field.FieldType, field.Name, engineVersion)
		{
		}

		public void Dump(int depth, Logger logger, List<TypeField> previouslyDumped = null)
		{
			if (previouslyDumped == null)
				previouslyDumped = new List<TypeField>();
			string logLine = "";
			for (int i = 0; i < depth; i++)
				logLine += ' ';
			string additionalInfo = "";
			if (this.isArray || this.hasAlignment)
			{
				additionalInfo += " (";
				if (this.isArray)
					additionalInfo += "array, ";
				if (this.hasAlignment)
					additionalInfo += "aligned, ";
				additionalInfo = additionalInfo.Substring(0, additionalInfo.Length-2) + ")";
			}
			logLine += this.type + " " + this.name + additionalInfo;
			logger.Write(logLine);

			if (previouslyDumped.Contains(this._base))
				return;
			previouslyDumped.Add(this._base);
			for (int i = 0; i < children.Count; i++)
			{
				children[i].Dump(depth + 1, logger, new List<TypeField>(previouslyDumped));
			}
        }
	}
}

