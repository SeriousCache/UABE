using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.IO;

namespace TypeTreeGenerator
{
    class ClassDatabaseFile2
    {
        List<TypeField> behaviourTypes = new List<TypeField>();
        static readonly byte[] binaryFileHeader = { (byte)'c', (byte)'l', (byte)'d', (byte)'b' };
        static readonly UInt32 fileHeaderUInt = 0x62646C63;

        private bool CheckEndlessRecursion(TypeField type, List<TypeField> parents)
        {
            parents.Add(type);
            foreach (TypeField child in type.children)
            {
                if (parents.Contains(child))
                    return true;
                if (CheckEndlessRecursion(child, new List<TypeField>(parents)))
                    return true;
            }
            return false;
        }

        public void Add(TypeField type)
        {
            if (CheckEndlessRecursion(type, new List<TypeField>()))
            {
                HelperClass.logger.Warning("Type \"" + type.type + "\" is not serializable (endless recursion)!");
            }
            else if (!behaviourTypes.Contains(type))
            {
                behaviourTypes.Add(type);
            }
        }


        private static byte[] MakeCChar(string str)
        {
            byte[] ret = new byte[str.Length + 1];
            for (int i = 0; i < str.Length; i++)
                ret[i] = (byte)str[i];
            ret[ret.Length - 1] = 0;
            return ret;
        }
        private static int AddToStringTable(MemoryStream table, string str)
        {
            byte[] strCChar = MakeCChar(str);
            int ret = (int)table.Position;
            table.Seek(0, SeekOrigin.Begin);
            byte[] stringTableBuf = table.GetBuffer(); long stringTableLen = table.Length;
            if (stringTableLen > strCChar.Length)
            {
                bool found = false;
                for (int i = 0; i <= stringTableLen - strCChar.Length; i++)
                {
                    found = true;
                    for (int j = 0; j < strCChar.Length; j++)
                    {
                        if (stringTableBuf[i + j] != strCChar[j])
                        {
                            found = false;
                            break;
                        }
                    }
                    if (found)
                    {
                        table.Seek(ret, SeekOrigin.Begin);
                        return i;
                    }
                }
            }
            /*byte[] altBuffer = new byte[strCChar.Length];
            int tablePos = 0; int remainingBytes = ret;
            while (remainingBytes > str.Length)
            {
                table.Seek(tablePos, SeekOrigin.Begin);
                table.Read(altBuffer, 0, altBuffer.Length);
                bool buffersEqual = true;
                for (int i = 0; i < strCChar.Length; i++)
                {
                    if (altBuffer[i] != strCChar[i])
                    {
                        buffersEqual = false;
                        break;
                    }
                }
                if (buffersEqual)
                {
                    table.Seek(ret, SeekOrigin.Begin);
                    return tablePos;
                }
                remainingBytes--; tablePos++;
            }*/
            table.Seek(ret, SeekOrigin.Begin);
            table.Write(strCChar, 0, strCChar.Length);
            return ret;
        }


        private int WriteTypeField(MemoryStream stringTable, BinaryWriter writer, TypeField tfield, int depth, string overrideName = null)
        {
            int ret = 1;
            int fieldTypeStringOffset = AddToStringTable(stringTable, tfield.type);
            writer.Write(fieldTypeStringOffset);
            int fieldNameStringOffset = AddToStringTable(stringTable, (overrideName == null) ? tfield.name : overrideName);
            writer.Write(fieldNameStringOffset);
            writer.Write((byte)depth);
            writer.Write(tfield.isArray);
            writer.Write(tfield.size);
            writer.Write((ushort)tfield.version);
            writer.Write((UInt32)(tfield.hasAlignment ? 0x4000 : 0)); //flags2

            for (int k = 0; k < tfield.children.Count; k++)
            {
                TypeField childfield = tfield.children[k];
                ret += WriteTypeField(stringTable, writer, childfield, depth + 1);
            }
            return ret;
        }
        public void Write(BinaryWriter writer)
        {
            using (MemoryStream stringTable = new MemoryStream())
            {
                writer.Write(binaryFileHeader, 0, 4);
                writer.Write((byte)4); //fileVersion
                writer.Write((byte)1); //flags
                writer.Write((byte)0); //compressionType
                long header_FileSizes = writer.BaseStream.Position;
                writer.Write((UInt32)0); //compressedSize
                writer.Write((UInt32)0); //uncompressedSize
                writer.Write((byte)0); //assetsVersionCount
                writer.Flush();
                long header_StringTableLen = writer.BaseStream.Position;
                writer.Write((UInt32)0); //stringTableLen
                writer.Write((UInt32)0); //stringTablePos

                writer.Write((UInt32)(behaviourTypes.Count));
                for (int i = 0; i < behaviourTypes.Count; i++)
                {
                    writer.Write(-1); //classId
                    writer.Write(114); //baseClass (MonoBehaviour)
                    int monoTypeStringOffset = AddToStringTable(stringTable, behaviourTypes[i].monoType);
                    writer.Write(monoTypeStringOffset);
                    int baseModuleStringOffset = AddToStringTable(stringTable, behaviourTypes[i].baseModuleName);
                    writer.Write(baseModuleStringOffset);
                    long fieldCountPos = writer.BaseStream.Position;
                    writer.Write((UInt32)0);
                    int actualCount = WriteTypeField(stringTable, writer, behaviourTypes[i], 0, "Base");
                    long endPos = writer.BaseStream.Position;
                    writer.Seek((int)fieldCountPos, SeekOrigin.Begin);
                    writer.Write((UInt32)actualCount);
                    writer.Seek((int)endPos, SeekOrigin.Begin);
                }
                writer.Flush();
                long stringTablePos = writer.BaseStream.Position;
                writer.Write(stringTable.GetBuffer(), 0, (int)stringTable.Length);
                writer.Seek((int)header_FileSizes, SeekOrigin.Begin);
                writer.Write((UInt32)(stringTablePos + stringTable.Length)); //compressedSize
                writer.Write((UInt32)(stringTablePos + stringTable.Length)); //uncompressedSize
                writer.Seek((int)header_StringTableLen, SeekOrigin.Begin);
                writer.Write((UInt32)stringTable.Length); //stringTableLen
                writer.Write((UInt32)stringTablePos); //stringTablePos
                writer.Seek(0, SeekOrigin.End);
                writer.Flush();
            }
        }
        private string GetString(char[] buffer, UInt32 offset)
        {
            UInt32 len = 0;
            for (UInt32 i = offset; i < buffer.Length; i++)
            {
                if (buffer[i] == 0)
                {
                    len = i - offset;
                    break;
                }
            }
            if (len == 0)
                return String.Empty;
            char[] chars = new char[len];
            UInt32 charIndex = 0;
            for (UInt32 i = offset; charIndex < len; i++)
            {
                chars[charIndex] = buffer[i];
                charIndex++;
            }
            return new String(chars);
        }
        private char[] MakeStringTable(BinaryReader reader, long pos, uint len)
        {
            long oldFilePosition = reader.BaseStream.Position;
            reader.BaseStream.Position = pos;
            byte[] stringBuffer = new byte[len];
            reader.Read(stringBuffer, 0, (int)len);
            char[] ret = new char[len];
            for (uint i = 0; i < len; i++)
                ret[i] = (char)stringBuffer[i];
            reader.BaseStream.Position = oldFilePosition;
            return ret;
        }
        private void ReadTypeField(BinaryReader reader, char[] stringTable, StreamWriter dump)
        {
            UInt32 typeStringOffset = reader.ReadUInt32();
            UInt32 nameStringOffset = reader.ReadUInt32();
            string type = GetString(stringTable, typeStringOffset);
            string name = GetString(stringTable, nameStringOffset);
            byte depth = reader.ReadByte();
            bool isArray = reader.ReadBoolean();
            int size = reader.ReadInt32();
            int version = reader.ReadInt16();
            int flags2 = reader.ReadInt32();

            for (int i = 0; i < depth; i++)
                dump.Write(' ');

            bool hasAlignment = (flags2 & 0x4000) != 0;
            string additional = " (";
            additional += "size = " + size + ", ";
            if (isArray)
                additional += "array, ";
            if (hasAlignment)
                additional += "aligned, ";
            additional = additional.Substring(0, additional.Length - 2) + ")";
            dump.WriteLine(type + " " + name + additional);
        }
        public bool Read(BinaryReader reader, StreamWriter dump)
        {
            reader.BaseStream.Position = 0;
            UInt32 curHeaderUInt = reader.ReadUInt32();
            if (curHeaderUInt != fileHeaderUInt)
                return false;
            int version = reader.ReadByte();
            int flags = (version >= 4) ? reader.ReadByte() : 0;
            byte compressionType = reader.ReadByte();
            uint compressedSize = reader.ReadUInt32();
            uint uncompressedSize = reader.ReadUInt32();
            int assetsVersionCount = reader.ReadByte();
            for (int i = 0; i < assetsVersionCount; i++)
                reader.ReadByte();
            UInt32 stringTableLength = reader.ReadUInt32();
            UInt32 stringTablePos = reader.ReadUInt32();
            char[] stringTable = MakeStringTable(reader, stringTablePos, stringTableLength);

            UInt32 typeListLen = reader.ReadUInt32();
            for (UInt32 i = 0; i < typeListLen; i++)
            {
                int classId = reader.ReadInt32();
                int baseClassId = reader.ReadInt32();
                if ((flags & 1) == 0)
                {
                    UInt32 stringTable_typeNameOffs = reader.ReadUInt32();
                    string typeName = GetString(stringTable, stringTable_typeNameOffs);
                    dump.WriteLine("Type \"" + typeName + "\"");
                }
                else
                {
                    UInt32 stringTable_typeNameOffs = reader.ReadUInt32();
                    string typeName = GetString(stringTable, stringTable_typeNameOffs);
                    UInt32 stringTable_moduleNameOffs = reader.ReadUInt32();
                    string moduleName = GetString(stringTable, stringTable_moduleNameOffs);
                    dump.WriteLine("Type \"" + typeName + "\" (" + moduleName + ")");
                }
                int childCount = reader.ReadInt32();
                for (int k = 0; k < childCount; k++)
                    ReadTypeField(reader, stringTable, dump);
            }
            return true;
        }
    }
}
