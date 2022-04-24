using System;
using Mono.Cecil;
using Mono.Cecil.Cil;
using Mono.Cecil.Rocks;
using System.Reflection;
using System.Collections.Generic;

namespace TypeTreeGenerator
{
	public class HelperClass
	{
		public static Logger logger = null;
		public static void SetLogger(Logger _logger)
		{
			logger = _logger;
		}

		public static GenericFuncContainer<T,bool> MemberNameComparer<T>(string name) where T : IMemberDefinition
		{
			return new GenericFuncContainer<T,bool>(member => {
				return member.Name.Equals(name);
			});
        }
        public static GenericFuncContainer<T, bool> MemberFullNameComparer<T>(string name) where T : IMemberDefinition
        {
            return new GenericFuncContainer<T, bool>(member => {
                return member.FullName.Equals(name);
            });
        }
        //doesn't work on System.Object
        public static GenericFuncContainer<T,bool> DeclaringTypeBaseTypeComparer<T>(TypeDefinition baseType) where T : IMemberDefinition
		{
			return new GenericFuncContainer<T,bool>(member => {
				TypeDefinition curBaseType = member.DeclaringType;
				while (curBaseType != null && curBaseType.BaseType != null)
				{
					if (curBaseType.Equals(baseType))
						return true;
					curBaseType = curBaseType.BaseType.Resolve();
				}
				return false;
			});
		}
		public static GenericFuncContainer<TypeDefinition,bool> BaseInterfaceComparer(TypeDefinition baseType)
		{
			return new GenericFuncContainer<TypeDefinition,bool>(type => {
				TypeDefinition curBaseType = type;
				while (curBaseType != null)
				{
					foreach (TypeReference curInterfaceRef in curBaseType.Interfaces)
					{
						TypeDefinition interfaceDef = curInterfaceRef.Resolve();
						if (interfaceDef == null)
							continue;
						if (interfaceDef.Equals(baseType))
							return true;
					}
					TypeReference nextBaseType = curBaseType.BaseType;
					if (nextBaseType == null)
						break;
					curBaseType = nextBaseType.Resolve();
				}
				return false;
			});
		}
		public static GenericFuncContainer<TypeDefinition,bool> CustomAttributeTypeComparer(string attrName)
		{
			//logger.KeyInfo("baseType = " + baseType.FullName);
			return new GenericFuncContainer<TypeDefinition,bool>(type => {
				foreach (CustomAttribute attr in type.CustomAttributes)
				{
					if (attr.AttributeType.FullName.Equals(attrName))
						return true;
				}
				return false;
			});
		}
		public static GenericFuncContainer<TypeDefinition,bool> BaseTypeComparer(TypeDefinition baseType)
		{
			//logger.KeyInfo("baseType = " + baseType.FullName);
			return new GenericFuncContainer<TypeDefinition,bool>(type => {
				TypeDefinition curBaseType = type;
				while (curBaseType != null)
				{
					if (curBaseType.Equals(baseType))
						return true;
					TypeReference nextBaseType = curBaseType.BaseType;
					if (nextBaseType == null)
						break;
					curBaseType = nextBaseType.Resolve();
				}
				return false;
			});
		}
		public static GenericFuncContainer<TypeDefinition,bool> BaseTypeComparer(string baseType)
		{
			//logger.KeyInfo("baseType = " + baseType.FullName);
			return new GenericFuncContainer<TypeDefinition,bool>(type => {
				TypeDefinition curBaseType = type;
				while (curBaseType != null)
				{
					if (curBaseType.FullName.Equals(baseType))
						return true;
					TypeReference nextBaseType = curBaseType.BaseType;
					if (nextBaseType == null)
						break;
					curBaseType = nextBaseType.Resolve();
				}
				return false;
			});
		}
		public static string GetFullName(TypeReference tref)
		{
			string ret = tref.Namespace;
			if (ret.Length > 0)
				ret += ".";
			ret += tref.Name;
			if (tref is GenericInstanceType)
			{
				System.Text.StringBuilder genBuilder = new System.Text.StringBuilder();
				GenericInstanceType gref = (GenericInstanceType)tref;
				for (int i = 0; i < gref.GenericArguments.Count; i++)
				{
					genBuilder.Append(GetFullName(gref.GenericArguments[i]));
					genBuilder.Append(", ");
				}
				if (genBuilder.Length > 0)
					genBuilder.Remove(genBuilder.Length-2, 2);
				ret += "<" + genBuilder.ToString() + ">";
			}
			return ret;
		}
		private static TypeReference ResolveGenericTypeRefs_(GenericInstanceType _base, TypeReference typeToResolve)
		{
			bool resolved = false;
			int arrayDims = 0;
			while (typeToResolve is ArrayType)
			{
				arrayDims++;
				typeToResolve = ((ArrayType)typeToResolve).ElementType;
			}
			if (typeToResolve is GenericParameter)
			{
				GenericParameter genPar = (GenericParameter)typeToResolve;
				//string fullName = genPar.DeclaringType.Resolve().FullName;
				//foreach (GenericInstanceType _base in bases)
				{
					//bool doBreak = false;
					//if (_base.Resolve().FullName.Equals(fullName))
					{
						for (int i = 0; i < _base.GenericArguments.Count; i++)
						{
							if (_base.Resolve().GenericParameters[i].FullName.Equals(genPar.FullName))
							{
								typeToResolve = _base.GenericArguments[i];
								resolved = true;
								//doBreak = true;
								break;
							}
						}
					}
					//if (doBreak)
					//	break;
				}
			}
			if (typeToResolve is GenericInstanceType)
			{
				GenericInstanceType genType = (GenericInstanceType)typeToResolve;
				TypeReference[] genericParameters = genType.GenericArguments.ToArray();
				for (int i = 0; i < genType.GenericArguments.Count; i++)
				{
					/*for (int k = 0; k < _base.GenericArguments.Count; k++)
					{
						if (_base.GenericParameters[k].FullName.Equals(genType.GenericParameters[i].FullName))
						{
							genericParameters[i] = _base.GenericArguments[k];
						}
					}*/
					genericParameters[i] = ResolveGenericTypeRefs_(_base, genericParameters[i]);
				}
                TypeReference elemType = genType.ElementType;
                if (elemType is GenericParameter)
                {
                    elemType = ResolveGenericTypeRefs_(_base, elemType);
                }
                if (elemType is GenericInstanceType)
                {
                    typeToResolve = ((GenericInstanceType)elemType).MakeGenericInstanceType(genericParameters);
                }
                else
                {
                    typeToResolve = elemType.Resolve().MakeGenericInstanceType(genericParameters);
                }
			}
			if (arrayDims > 0)
				typeToResolve = typeToResolve.MakeArrayType(arrayDims);
			return typeToResolve;
		}
		public static TypeReference ResolveGenericTypeRefs(TypeReference _base, TypeReference typeToResolve)
		{
			if (!(_base is GenericInstanceType))
				return typeToResolve;
			return ResolveGenericTypeRefs_((GenericInstanceType)_base, typeToResolve);
			//List<GenericInstanceType> bases = new List<GenericInstanceType>();
			//bases.Add((GenericInstanceType)_base);
			//TypeReference ret = ResolveGenericTypeRefs(bases, typeToResolve);
			//if (ret == null)
			//	return typeToResolve;
			//return ret;
		}
		protected static string writeGenericArgument(TypeReference tref)
		{
			if (tref.Resolve() == null)
				return "";
			System.Text.StringBuilder retBuilder = new System.Text.StringBuilder();
			retBuilder.Append(tref.Resolve().FullName);
			if (retBuilder.Length > 2 && retBuilder[retBuilder.Length-2] == '\u0060')
				retBuilder.Remove(retBuilder.Length-2,2);
			if (tref is GenericInstanceType && ((GenericInstanceType)tref).GenericArguments.Count > 0)
			{
				GenericInstanceType gitPar = (GenericInstanceType)tref;
				if (gitPar.GenericArguments.Count > 0)
					retBuilder.Append('<');
				foreach (TypeReference _gpar in gitPar.GenericArguments) {
					retBuilder.Append(writeGenericArgument(_gpar));
				}
				if (gitPar.GenericArguments.Count > 0)
					retBuilder[retBuilder.Length-1] = '>';
			}
			return retBuilder.ToString() + ",";
		}
		public static GenericFuncContainer<FieldDefinition,bool> FieldTypeComparer(string fieldType)
		{
			return new GenericFuncContainer<FieldDefinition,bool>(field => {
				TypeReference tref = field.FieldType; 
				TypeDefinition type = field.FieldType.Resolve();
				if (type == null)
					return false;
				System.Text.StringBuilder typeNameBuilder = new System.Text.StringBuilder();
				typeNameBuilder.Append(type.FullName);
				if ((type.GenericParameters.Count > 0) && (tref is GenericInstanceType))
				{
					typeNameBuilder.Remove(typeNameBuilder.Length-2,2);
					typeNameBuilder.Append('<');
					foreach (TypeReference garg in ((GenericInstanceType)tref).GenericArguments)
					{
						typeNameBuilder.Append(writeGenericArgument(garg));
					}
					typeNameBuilder[typeNameBuilder.Length-1] = '>';
				}
				return typeNameBuilder.ToString().Equals(fieldType);
			});
		}
		public static GenericFuncContainer<FieldDefinition,bool> FieldAttributeComparer(Mono.Cecil.FieldAttributes attrs)
		{
			return new GenericFuncContainer<FieldDefinition,bool>(field => {
				return (field.Attributes & attrs) == attrs;
			});
		}
		public static GenericFuncContainer<FieldDefinition,bool> FieldNegAttributeComparer(Mono.Cecil.FieldAttributes negAttrs)
		{
			return new GenericFuncContainer<FieldDefinition,bool>(field => {
				return (field.Attributes & negAttrs) == 0;
			});
		}
		public static GenericFuncContainer<MethodDefinition,bool> MethodAttributeComparer(Mono.Cecil.MethodAttributes attrs)
		{
			return new GenericFuncContainer<MethodDefinition,bool>(method => {
				return (method.Attributes & attrs) == attrs;
			});
		}
		public static GenericFuncContainer<MethodDefinition,bool> MethodNegAttributeComparer(Mono.Cecil.MethodAttributes negAttrs)
		{
			return new GenericFuncContainer<MethodDefinition,bool>(method => {
				return (method.Attributes & negAttrs) == 0;
			});
		}
		public static GenericFuncContainer<TypeDefinition,bool> TypeAttributeComparer(Mono.Cecil.TypeAttributes attrs)
		{
			return new GenericFuncContainer<TypeDefinition,bool>(type => {
				return (type.Attributes & attrs) == attrs;
			});
		}
		public static GenericFuncContainer<TypeDefinition,bool> TypeNegAttributeComparer(Mono.Cecil.TypeAttributes negAttrs)
		{
			return new GenericFuncContainer<TypeDefinition,bool>(type => {
				return (type.Attributes & negAttrs) == 0;
			});
		}
		public static GenericFuncContainer<MethodDefinition,bool> MethodReturnTypeComparer(string returnType)
		{
			return new GenericFuncContainer<MethodDefinition,bool>(method => {
				return method.ReturnType.FullName.Equals(returnType);
			});
		}
		public static GenericFuncContainer<MethodDefinition,bool> MethodReturnTypeComparer(TypeDefinition returnType)
		{
			return new GenericFuncContainer<MethodDefinition,bool>(method => {
				TypeDefinition curReturnType = method.ReturnType.Resolve();
				if (curReturnType == null)
				{
					HelperClass.OnError(ErrorCode.RETURNTYPE_RESOLVE_ERROR, method.FullName, Environment.StackTrace);
					return false;
				}
				return curReturnType.Equals(returnType);
			});
		}
		public static GenericFuncContainer<MethodDefinition,bool> MethodParametersComparerEx(params string[] parameterTypes)
		{
			return new GenericFuncContainer<MethodDefinition,bool> (method => {
				if (method.Parameters.Count != parameterTypes.Length)
					return false;
				for (int i = 0; i < method.Parameters.Count; i++)
				{
					if (parameterTypes[i].Length == 0)
						continue;
					TypeReference curParTref = method.Parameters[i].ParameterType;
					TypeDefinition curParTdef = curParTref.Resolve();
					if (curParTdef == null)
						throw new Exception("Unable to resolve the type '" + curParTref.FullName + "'!");
					System.Text.StringBuilder typeNameBuilder = new System.Text.StringBuilder ();
					typeNameBuilder.Append (curParTdef.FullName);
					if ((curParTdef.GenericParameters.Count > 0) && (curParTref is GenericInstanceType))
					{
						typeNameBuilder.Remove (typeNameBuilder.Length - 2, 2);
						typeNameBuilder.Append ('<');
						foreach (TypeReference garg in ((GenericInstanceType)curParTref).GenericArguments)
						{
							typeNameBuilder.Append (writeGenericArgument (garg));
						}
						typeNameBuilder [typeNameBuilder.Length - 1] = '>';
						if (curParTref.IsArray)
							typeNameBuilder.Append("[]");
					}
					if (!typeNameBuilder.ToString().Equals(parameterTypes[i]))
						return false;
				}
				return true;
			});
		}
		public static GenericFuncContainer<MethodDefinition,bool> MethodParametersComparer(params string[] parameterTypes)
		{
			return new GenericFuncContainer<MethodDefinition,bool>(method => {
				if (method.Parameters.Count != parameterTypes.Length)
					return false;
				for (int i = 0; i < method.Parameters.Count; i++)
				{
					if (parameterTypes[i].Length != 0 && !method.Parameters[i].ParameterType.FullName.Equals(parameterTypes[i]))
						return false;
				}
				return true;
			});
		}
		public static GenericFuncContainer<MethodDefinition,bool> MethodParametersComparer(TypeDefinition[] parameterTypes)
		{
			return new GenericFuncContainer<MethodDefinition,bool>(method => {
				if (method.Parameters.Count != parameterTypes.Length)
					return false;
				for (int i = 0; i < method.Parameters.Count; i++)
				{
					TypeDefinition curParameterType = method.Parameters[i].ParameterType.Resolve();
					if (curParameterType == null)
					{
						HelperClass.OnError(ErrorCode.PARAMETER_RESOLVE_ERROR, i, method.FullName, Environment.StackTrace);
						return false;
					}
					if (parameterTypes[i] != null && !curParameterType.Equals(parameterTypes[i]))
						return false;
				}
				return true;
			});
		}
		public static GenericFuncContainer<MethodDefinition,bool> MethodParameterNamesComparer(params string[] parameterNames)
		{
			return new GenericFuncContainer<MethodDefinition,bool>(method => {
				if (method.Parameters.Count != parameterNames.Length)
					return false;
				for (int i = 0; i < method.Parameters.Count; i++)
				{
					if (parameterNames[i].Length != 0 && !method.Parameters[i].Name.Equals(parameterNames[i]))
						return false;
				}
				return true;
			});
		}
		public static GenericFuncContainer<MethodDefinition,bool> MethodOPCodeComparer(int[] indices, OpCode[] opCodes, object[] operands)
		{
			if (indices.Length != opCodes.Length || (operands != null && operands.Length != opCodes.Length))
			{
				OnError(ErrorCode.INVALID_PARAMETER, "MethodOPCodeComparer : all arrays should have the same size");
				return null;
			}
			return new GenericFuncContainer<MethodDefinition,bool>(method => {
				Instruction[] instrs = method.Body.Instructions.ToArray();
				for (int i = 0; i < indices.Length; i++)
				{
					int index = indices[i] < 0 ? (instrs.Length + indices[i]) : indices[i];
					if ((index > instrs.Length) || (index < 0))
						return false;
					if (!HelperClass.OPMatches(instrs[index], opCodes[i], (operands != null) ? operands[i] : null))
						return false;
				}
				return true;
			});
		}

		//only returns true if all comparers return true
		//used for findType to make sure multiple attributes apply to a member
		public static GenericFuncContainer<T,bool> CombinedComparer<T>(params GenericFuncContainer<T,bool>[] childComparers)
		{
			return new GenericFuncContainer<T,bool>(member => {
				foreach (GenericFuncContainer<T,bool> comparer in childComparers)
				{
					if (!comparer.Execute(member))
						return false;
				}
				return true;
			});
		}
		//returns true if at least one of the comparers return true
		//used for findType to make sure multiple attributes apply to a member
		public static GenericFuncContainer<T,bool> CombinedORComparer<T>(params GenericFuncContainer<T,bool>[] childComparers)
		{
			return new GenericFuncContainer<T,bool>(member => {
				foreach (GenericFuncContainer<T,bool> comparer in childComparers)
				{
					if (comparer.Execute(member))
						return true;
				}
				return false;
			});
		}
		//used to compare attributes of a nested type's member
		public static GenericFuncContainer<TypeDefinition,bool> TypeMembersComparer(params FuncContainer[] childComparers)
		{
			return new GenericFuncContainer<TypeDefinition,bool>(type => {
				Dictionary<FuncContainer,bool> comparersApply = new Dictionary<FuncContainer, bool>();
				foreach (FuncContainer comparer in childComparers)
					comparersApply.Add(comparer, false);

				foreach (MethodDefinition method in type.Methods)
					compareMember<MethodDefinition>(method, comparersApply);
				foreach (FieldDefinition field in type.Fields)
					compareMember<FieldDefinition>(field, comparersApply);
				foreach (PropertyDefinition property in type.Properties)
					compareMember<PropertyDefinition>(property, comparersApply);
				foreach (TypeDefinition curType in type.NestedTypes)
					compareMember<TypeDefinition>(curType, comparersApply);

				foreach (KeyValuePair<FuncContainer,bool> comparerApplies in comparersApply)
				{
					if (!comparerApplies.Value)
					{
						return false;
					}
				}
				return true;
			});
		}

		public static T findMember<T>(ModuleDefinition module, object type, bool allowMultipleResults, bool mustHaveResult, params GenericFuncContainer<T,bool>[] comparers)
			where T : IMemberDefinition
		{
			T[] ret = findMembers<T>(module, type, comparers);
			List<T> nullContainer = new List<T>();
			//workaround for compiler errors (I assume that nobody tries to create a value type implementing IMemberDefinition..)
			nullContainer.GetType().GetMethod("Add", new Type[]{typeof(T)}).Invoke(nullContainer, new object[]{ null });
			if (ret == null || ret.Length == 0)
			{
				if (mustHaveResult)
					OnError(ErrorCode.MEMBER_NOT_FOUND, typeof(T).Name, Environment.StackTrace);
				return nullContainer[0];
			}
			//if (ret.Length == 0)
			//	return nullContainer[0];
			if (!allowMultipleResults && ret.Length > 1)
			{
				OnError(ErrorCode.MULTIPLE_RESULTS, typeof(T).Name, Environment.StackTrace);
				return nullContainer[0];
			}
			return ret[0];
		}
		public static T findMember<T>(ModuleDefinition module, object type, bool allowMultipleResults, params GenericFuncContainer<T,bool>[] comparers)
			where T : IMemberDefinition
		{
			return findMember<T>(module, type, allowMultipleResults, true, comparers);
		}
		public static T[] findMembers<T>(ModuleDefinition module, object type, params GenericFuncContainer<T,bool>[] comparers)
			where T : IMemberDefinition
		{
			if (type != null)
			{
				TypeDefinition tdef = (type is string) ? module.GetType((string)type) : ((type is TypeDefinition) ? ((TypeDefinition)type) : null);
				if (tdef == null) {
					OnError (ErrorCode.TYPE_NOT_FOUND, (type is string) ? ((string)type) : "(null)");
					return null;
				}
				T[] memberArray = null;
				object memberCollection = null;
				if (typeof(T) == typeof(MethodDefinition))
					memberCollection = tdef.Methods;
				else if (typeof(T) == typeof(FieldDefinition))
					memberCollection = tdef.Fields;
				else if (typeof(T) == typeof(PropertyDefinition))
					memberCollection = tdef.Properties;
				else if (typeof(T) == typeof(TypeDefinition))
					memberCollection = tdef.NestedTypes;
				if (memberCollection == null)
					throw new NotSupportedException ("member type " + typeof(T).Name);
				//another workaround for compiler errors (when converting to T[])
				memberArray = (T[])memberCollection.GetType().GetMethod("ToArray", new Type[0]).Invoke(memberCollection, new object[0]);
				List<T> members = new List<T>();
				foreach (T member in memberArray)
				{
					bool matches = true;
					foreach (GenericFuncContainer<T,bool> comparer in comparers)
					{
						if (comparer == null || !comparer.Execute(member)) {
							matches = false;
							break;
						}
					}
					if (matches)
						members.Add(member);
				}
				return members.ToArray();
			}
			else
			{
				List<T> members = new List<T>();
				foreach (TypeDefinition tdef in module.Types)
					members.AddRange(findMembers<T>(module, tdef, comparers));
				return members.ToArray();
			}
		}

		//comparers : Dictionary<FuncContainer comparer, bool doesApply>
		//	doesApply : set to true if the comparer returned true; not set to false otherwise
		private static void compareMember<T>(T member, Dictionary<FuncContainer,bool> comparers)
		{
			FuncContainer[] keys = new FuncContainer[comparers.Count];
			comparers.Keys.CopyTo(keys, 0);
			for (int i = 0; i < comparers.Count; i++)
			{
				FuncContainer comparer = keys[i];
				if (comparer.GetArgType().Equals(typeof(T)) && ((bool)comparer.Execute(member)))
				{
					comparers[comparer] = true;
				}
			}
		}

		//Executes the comparers on all matching type members.
		//(Almost) The same as getting the declaring type of findMembers with a null type,
		//  except that findTypes works with multiple member types to compare.
		public static TypeDefinition[] findTypes(ModuleDefinition module, params FuncContainer[] comparers)
		{
			List<TypeDefinition> ret = new List<TypeDefinition>();

			Dictionary<FuncContainer,bool> comparersApply = new Dictionary<FuncContainer, bool>();
			foreach (FuncContainer comparer in comparers)
				comparersApply.Add(comparer, false);

			foreach (TypeDefinition type in module.Types)
			{
				compareMember<TypeDefinition>(type, comparersApply);
				foreach (MethodDefinition method in type.Methods)
					compareMember<MethodDefinition>(method, comparersApply);
				foreach (FieldDefinition field in type.Fields)
					compareMember<FieldDefinition>(field, comparersApply);
				foreach (PropertyDefinition property in type.Properties)
					compareMember<PropertyDefinition>(property, comparersApply);
				foreach (TypeDefinition curType in type.NestedTypes)
					compareMember<TypeDefinition>(curType, comparersApply);

				bool typeDoesMatch = true;
				foreach (KeyValuePair<FuncContainer,bool> comparerApplies in comparersApply)
				{
					if (!comparerApplies.Value)
					{
						typeDoesMatch = false;
						break;
					}
				}
				if (typeDoesMatch)
					ret.Add(type);

				foreach (FuncContainer comparer in comparers)
					comparersApply[comparer] = false;
			}
			return ret.ToArray();
		}
		//Executes the comparers on all matching type members.
		//(Almost) The same as getting the declaring type of findMember with a null type,
		//  except that findType works with multiple member types to compare.
		public static TypeDefinition findType(ModuleDefinition module, bool allowMultipleResults, params FuncContainer[] comparers)
		{
			TypeDefinition[] ret = findTypes(module, comparers);
			if (ret == null || ret.Length == 0)
			{
				OnError(ErrorCode.MEMBER_NOT_FOUND, "TypeDefinition", Environment.StackTrace);
				return null;
			}
			if (!allowMultipleResults && ret.Length > 1)
			{
				OnError(ErrorCode.MULTIPLE_RESULTS, "TypeDefinition", Environment.StackTrace);
				return null;
			}
			return ret[0];
		}

		public static Func<MethodDefinition,bool> MethodAttributeSetter(Mono.Cecil.MethodAttributes attrs)
		{
			return method => {
				method.Attributes = (method.Attributes & ~attrs) | attrs;
				return true;
			};
		}
		public static Func<T,bool> MemberNameSetter<T>(string name)
			where T : IMemberDefinition
		{
			return member => {
				member.Name = name;
				return true;
			};
		}
		public static void executeActions<T>(ModuleDefinition module, object type, GenericFuncContainer<T,bool>[] comparers, params Func<T,bool>[] actions)
			where T : IMemberDefinition
		{
			T[] members = findMembers<T>(module, type, comparers);
			if (members.Length == 0)
				OnError (ErrorCode.MEMBER_NOT_FOUND, typeof(T).Name, Environment.StackTrace);
			foreach (T member in members)
			{
				foreach (Func<T,bool> action in actions)
				{
					if (!action(member))
						OnError(ErrorCode.ACTION_FAILED, "member " + member.FullName);
				}
				logger.Info("Patched " + member.FullName + ".");
			}
		}
		public static void executeActions(ModuleDefinition module, object type, params GenericFuncContainer<TypeDefinition,bool>[] actions)
		{
			TypeDefinition tdef = (type is string) ? module.GetType((string)type) : ((type is TypeDefinition) ? ((TypeDefinition)type) : null);
			if (tdef == null) {
				OnError(ErrorCode.TYPE_NOT_FOUND, (type is string) ? ((string)type) : "(null)");
				return;
			}
			foreach (GenericFuncContainer<TypeDefinition,bool> action in actions)
			{
				if (!action.Execute(tdef))
					OnError(ErrorCode.ACTION_FAILED, "type " + tdef.FullName);
			}
			logger.Info("Patched " + tdef.FullName + ".");
		}
		public static void executeActions<T>(T type, params GenericFuncContainer<T,bool>[] actions)
			where T : IMemberDefinition
		{
			string fullName;
			PropertyInfo fullNameProp = type.GetType().GetProperty("FullName");
			if (fullNameProp != null)
				fullName = (string)fullNameProp.GetGetMethod().Invoke(type, new object[0]);
			else
				fullName = type.ToString();

			foreach (GenericFuncContainer<T,bool> action in actions)
			{
				if (!action.Execute(type))
					OnError(ErrorCode.ACTION_FAILED, fullName);
			}
			logger.Info("Patched " + type.FullName + ".");
		}

		protected static bool OPMatches(Instruction instr, OpCode op, object operand)
		{
			if (instr.OpCode != op ||
				(
					(operand != null) ? 
					((operand != null && instr.Operand == null) || 
						!operand.Equals(instr.Operand)) 
					: false
				)) 
			{
				return false;
			}
			return true;
		}

		public static int[] FindOPCodePattern(MethodDefinition mdef, OpCode[] pattern, int offset = 0, object[] operands = null)
		{
			if (pattern.Length == 0)
				return new int[0];
			Mono.Collections.Generic.Collection<Instruction> instrs = mdef.Body.Instructions;
			List<int> results = new List<int>();
			for (int i = 0; i < (instrs.Count-pattern.Length); i++)
			{
				bool matches = true;
				for (int _i = 0; _i < pattern.Length; _i++)
				{
					Instruction curInstr = instrs[i+_i];
					if (!OPMatches(curInstr, pattern[_i], (operands == null) ? null : operands[_i])) 
					{
						matches = false;
						break;
					}
				}
				if (matches)
					results.Add(i + offset);
			}
			return results.ToArray();
		}

		public static bool RenameVirtualMethod(MethodDefinition method, string newName)
		{
			TypeDefinition[] parameters = new TypeDefinition[method.Parameters.Count];
			string oldName = method.Name;
			for (int i = 0; i < parameters.Length; i++)
			{
				parameters[i] = method.Parameters[i].ParameterType.Resolve ();
				if (parameters[i] == null)
				{
					HelperClass.OnError(ErrorCode.PARAMETER_RESOLVE_ERROR, i, method.FullName, Environment.StackTrace);
					return false;
				}
			}

			GenericFuncContainer<MethodDefinition,bool> vMethodComparer = CombinedComparer(
				MethodParametersComparer(parameters), 
				MemberNameComparer<MethodDefinition>(method.Name), 
				MethodAttributeComparer(Mono.Cecil.MethodAttributes.Virtual));

			TypeDefinition curBaseType = method.DeclaringType;
			while (curBaseType != null)
			{
				if (findMember<MethodDefinition>(null, curBaseType, true, false, vMethodComparer) == null)
					break;
				TypeReference curBaseRef = curBaseType.BaseType;
				if (curBaseRef == null)
					break;
				TypeDefinition curBaseDef = curBaseRef.Resolve();
				if (curBaseDef == null)
					break;
				curBaseType = curBaseDef;
			}

			vMethodComparer = CombinedComparer(vMethodComparer, DeclaringTypeBaseTypeComparer<MethodDefinition>(curBaseType));
			//doesn't work for multiple assemblies
			foreach (MethodDefinition curVMethod in findMembers<MethodDefinition>(curBaseType.Module, null, vMethodComparer))
			{
				curVMethod.Name = newName;
			}
			return true;
		}

		public interface FuncContainer
		{
			Type GetArgType();
			Type GetRetType();
			object Execute(object arg1);
		}
		public class GenericFuncContainer<T,K> : FuncContainer
		{
			private static MethodInfo genericExecuteMethod;
			static GenericFuncContainer()
			{
				genericExecuteMethod = typeof(GenericFuncContainer<T,K>).GetMethod("Execute", new Type[]{typeof(T)});
			}
			public Func<T,K> value;
			public GenericFuncContainer(Func<T,K> value)
			{
				this.value = value;
			}
			public Type GetArgType()
			{
				return typeof(T);
			}
			public Type GetRetType()
			{
				return typeof(K);
			}
			public K Execute(T arg1)
			{
				return value(arg1);
			}
			public object Execute(object arg1)
			{
				//T generic_arg1;
				if (arg1 == null) {
					if (typeof(T).IsValueType)
						throw new InvalidCastException("arg1 must be a boxed value!");
					//List<T> nullContainer = new List<T>();
					//workaround for compiler errors (I already checked if it is a value type before)
					//nullContainer.GetType().GetMethod("Add", new Type[]{typeof(T)}).Invoke(nullContainer, new object[]{ null });
					//generic_arg1 = nullContainer[0];
				}
				else if (!typeof(T).IsAssignableFrom(arg1.GetType()))
					throw new InvalidCastException("Cannot assign arg1 to " + typeof(T).FullName + "!");
				else
				{
					//generic_arg1 = arg1 as T;
				}
				return genericExecuteMethod.Invoke(this, new object[]{ arg1 });
			}
		}

		class ErrorInfo : Attribute
		{
			public string description;
			public int code;
			public ErrorInfo(string description, int code)
			{
				this.description = description;
				this.code = code;
			}
		}
		private enum ErrorCode
		{
			[ErrorInfo("Unable to find type {0}!", 0)]
			TYPE_NOT_FOUND,
			[ErrorInfo("Multiple results of type {0} found!\r\n{1}", 1)]
			MULTIPLE_RESULTS,
			[ErrorInfo("Unable to find a member of type {0}!\r\n{1}", 2)]
			MEMBER_NOT_FOUND,
			[ErrorInfo("Unable to apply patches to {0}!", 3)]
			ACTION_FAILED,
			[ErrorInfo("Invalid parameter : {0}!", 4)]
			INVALID_PARAMETER,
			[ErrorInfo("Unable to resolve the type of parameter {0} of {1}!\r\n{2}", 5)]
			PARAMETER_RESOLVE_ERROR,
			[ErrorInfo("Unable to resolve the return type of {1}!\r\n{2}", 6)]
			RETURNTYPE_RESOLVE_ERROR,
		};
		private static void OnError(ErrorCode error, params object[] args)
		{
			if (logger == null)
				return;
			MemberInfo[] codeInfo = typeof(ErrorCode).GetMember(error.ToString());
			if (codeInfo == null || codeInfo.Length <= 0)
			{
				logger.Error("Something really bad happened while executing OnError (cannot find " + error.ToString() + " in ErrorCode).");
				return;
			}
			object[] errorInfoAttrs = codeInfo[0].GetCustomAttributes(typeof(ErrorInfo), false);
			if (errorInfoAttrs.Length == 1 && errorInfoAttrs[0] is ErrorInfo) 
			{
				ErrorInfo errInfo = (ErrorInfo)errorInfoAttrs[0];
				logger.Error(String.Format(errInfo.description, args));
			}
			else
				logger.Error("OnError : cannot find the ErrorInfo attribute for " + error.ToString() + ".");
		}
	}
}

