using System;
using System.IO;
using System.Text;
using System.Collections.Generic;
using Mono.Cecil;

namespace TypeTreeGenerator
{
	class MainClass
    {
        private static byte[] makeStderrMessage(String header, String errorMessage)
        {
            int headerLen = Encoding.Unicode.GetByteCount(header);
            int messageLen = Encoding.Unicode.GetByteCount(errorMessage);
            byte[] message = new byte[8 + headerLen + messageLen];
            message[0] = (byte)((uint)headerLen & 0xFF); message[4] = (byte)((uint)messageLen & 0xFF);
            message[1] = (byte)(((uint)headerLen >> 8) & 0xFF); message[5] = (byte)(((uint)messageLen >> 8) & 0xFF);
            message[2] = (byte)(((uint)headerLen >> 16) & 0xFF); message[6] = (byte)(((uint)messageLen >> 16) & 0xFF);
            message[3] = (byte)(((uint)headerLen >> 24) & 0xFF); message[7] = (byte)(((uint)messageLen >> 24) & 0xFF);
            Encoding.Unicode.GetBytes(header, 0, header.Length, message, 8);
            Encoding.Unicode.GetBytes(errorMessage, 0, errorMessage.Length, message, 8 + headerLen);
            return message;
        }
        class AssemblyArgs
        {
            public string path;
            public EngineVersion engineVersion;
            public AssemblyArgs(string path, EngineVersion engineVersion)
            {
                this.path = path;
                this.engineVersion = engineVersion;
            }
        }

        static Logger logger;
        public static void Main(string[] args)
        {
            /*args = new string[]{ "-f", "E:\\Programme\\SteamLibrary\\SteamApps\\common\\7 Days To Die\\7DaysToDie_Data\\Managed\\Assembly-CSharp.dll",
                                "-f", "E:\\Programme\\SteamLibrary\\SteamApps\\common\\7 Days To Die\\7DaysToDie_Data\\Managed\\Assembly-CSharp-firstpass.dll"};*/
            //args = new string[] { "-f", @"P:\Downloads\users_assetbundle-demo\demo\build9 test\test_Data\Managed\Assembly-CSharp.dll" };
            //args = new string[] { "-f", @"P:\Programme\Unity 5.6.0f3\Editor\Data\Managed\UnityEngine.dll", "-f", @"P:\Downloads\UABEHelp31032018_Assembly-CSharp_RefFix.dll", "-c", "UnityTest.BoolComparer" };
            if (args.Length == 0)
            {
                Console.Out.WriteLine("TypeTreeGenerator Parameters (evaluated from first to last) :");
                Console.Out.WriteLine("-longpathid 0|1 : Generate int (0) or SInt64 (1) PathIDs in PPtrs.");
                Console.Out.WriteLine("-ver <year> <release> : Specify the Unity engine version for following files.");
                Console.Out.WriteLine("-f <file name> : Extract the type information from an assembly file. Can be used multiple times.");
                Console.Out.WriteLine("-c <full class name> : Only extract the type information from a specific class. Can be used multiple times.");
                Console.Out.WriteLine("-stdout : Write the binary ClassDatabaseFile to stdout instead of a text representation.");
                Console.Out.WriteLine("-stdin : Read more parameters from stdin (UTF-16 LE) separated with null terminators, ending with two terminators.");
                return;
            }
            List<string> argsList = new List<string>(args);
            EngineVersion engineVersion = new EngineVersion(0, 0);
            List<AssemblyArgs> targetAssemblies = new List<AssemblyArgs>();
            List<string> targetClasses = new List<string>();
            bool outputToStdout = false;
            for (int i = 0; i < argsList.Count; i++)
            {
                if (argsList[i].Equals("-stdout"))
                {
                    outputToStdout = true;
                }
                else if (argsList[i].Equals("-stdin"))
                {
                    byte[] inBytes;
                    using (Stream stream = Console.OpenStandardInput())
                    {
                        using (MemoryStream memStream = new MemoryStream())
                        {
                            byte[] tempBuffer = new byte[2048];
                            while (true)
                            {
                                int count = stream.Read(tempBuffer, 0, tempBuffer.Length);
                                if (count == 0)
                                {
                                    memStream.WriteByte(0);
                                    memStream.WriteByte(0);
                                    break;
                                }
                                memStream.Write(tempBuffer, 0, count);
                                if (memStream.Length > 4)
                                {
                                    memStream.Seek(-4, SeekOrigin.End);
                                    byte[] nullBuf = new byte[4];
                                    memStream.Read(nullBuf, 0, 4);
                                    if (nullBuf[0] == 0 && nullBuf[1] == 0 && nullBuf[2] == 0 && nullBuf[3] == 0)
                                        break;
                                }
                            }
                            inBytes = new byte[memStream.Length];
                            Array.Copy(memStream.GetBuffer(), inBytes, inBytes.Length);
                        }
                    }
                    int strBegin = 0;
                    for (int k = 0; k < (inBytes.Length - 1); k += 2)
                    {
                        if (inBytes[k] == 0 && inBytes[k + 1] == 0) //string end
                        {
                            if (k > strBegin)
                            {
                                argsList.Add(Encoding.Unicode.GetString(inBytes, strBegin, k - strBegin));
                            }
                            strBegin = k + 2;
                        }
                    }
                }
                else if (argsList[i].Equals("-longpathid"))
                {
                    if ((i + 1) < argsList.Count)
                    {
                        if (argsList[i + 1].Equals("1") || argsList[i + 1].Equals("true") || argsList[i + 1].Equals("on"))
                        {
                            if (engineVersion.year < 5)
                            {
                                engineVersion.year = 5;
                                engineVersion.release = 0;
                            }
                        }
                        else
                        {
                            if (engineVersion.year >= 5)
                            {
                                engineVersion.year = 0;
                                engineVersion.release = 0;
                            }
                        }
                        i++;
                    }
                }
                else if (argsList[i].Equals("-ver"))
                {
                    if ((i + 2) < argsList.Count)
                    {
                        uint.TryParse(argsList[i + 1], out engineVersion.year);
                        uint.TryParse(argsList[i + 2], out engineVersion.release);
                        i += 2;
                    }
                }
                else if (argsList[i].Equals("-f"))
                {
                    if ((i + 1) < argsList.Count)
                    {
                        targetAssemblies.Add(new AssemblyArgs(argsList[i + 1], engineVersion));
                        i++;
                    }
                }
                else if (argsList[i].Equals("-c"))
                {
                    if ((i + 1) < argsList.Count)
                    {
                        targetClasses.Add(argsList[i + 1]);
                        i++;
                    }
                }
            }
            if (targetAssemblies.Count == 0)
                return;

            HelperClass.GenericFuncContainer<TypeDefinition, bool> typeComparer;
            if (targetClasses.Count > 0)
            {
                var typeNameComparers = new HelperClass.GenericFuncContainer<TypeDefinition, bool>[targetClasses.Count];
                for (int i = 0; i < targetClasses.Count; i++)
                    typeNameComparers[i] = HelperClass.MemberFullNameComparer<TypeDefinition>(targetClasses[i]);
                typeComparer = HelperClass.CombinedORComparer(typeNameComparers);
            }
            else
            {
                typeComparer = HelperClass.CombinedORComparer(
                    HelperClass.BaseTypeComparer("UnityEngine.Object"),
                    HelperClass.TypeAttributeComparer(TypeAttributes.Serializable)
                );
            }

            Stream stderr = null;
            if (outputToStdout)
            {
                Console.Out.Close();
                Console.Error.Close();
                stderr = Console.OpenStandardError();
            }
            logger = new Logger(outputToStdout ? null : "typetreelog.txt", null, outputToStdout ? Int32.MaxValue : (int)Logger.Level.INFO);
            HelperClass.SetLogger(logger);
            ClassDatabaseFile2 databaseFile = new ClassDatabaseFile2();
            int curFieldIndex = 0;

            DefaultAssemblyResolver resolver = new DefaultAssemblyResolver();

            for (int i = 0; i < targetAssemblies.Count; i++)
            {
                FileInfo fileInfo = new FileInfo(targetAssemblies[i].path);
                resolver.AddSearchDirectory(fileInfo.Directory.FullName);
            }

            for (int i = 0; i < targetAssemblies.Count; i++)
            {
                FileInfo fileInfo = new FileInfo(targetAssemblies[i].path);
                try
                {
                    AssemblyDefinition csharpDef = AssemblyDefinition.ReadAssembly(targetAssemblies[i].path, new ReaderParameters { AssemblyResolver = resolver });
                    TypeDefinition[] typesToCheck = HelperClass.findTypes(csharpDef.Modules[0], typeComparer);
                    foreach (TypeDefinition tDef in typesToCheck)
                    {
                        if (tDef.HasGenericParameters)
                            continue;
                        if (tDef.IsAbstract)
                            continue;
                        try
                        {
                            TypeField baseField = new TypeField(csharpDef.Modules[0].Import(tDef), "Base", targetAssemblies[i].engineVersion, new List<TypeField>(), true);
                            baseField.baseModuleName = fileInfo.Name;
                            databaseFile.Add(baseField);
                            baseField.Dump(0, logger);
                            curFieldIndex++;
                        }
                        catch (Exception e)
                        {
                            if (outputToStdout)
                            {
                                byte[] message = makeStderrMessage(fileInfo.Name + " : " + tDef.FullName, e.ToString());
                                stderr.Write(message, 0, message.Length);
                                //Console.Error.WriteLine(message);
                            }
                            else
                            {
                                string message = "Failed at " + fileInfo.Name + " : " + tDef.FullName + "\n" + e.ToString();
                                logger.Error(message);
                            }
                            //return;
                        }
                    }
                }
                catch (Exception e)
                {
                    if (outputToStdout)
                    {
                        byte[] message = makeStderrMessage(fileInfo.Name, e.ToString());
                        stderr.Write(message, 0, message.Length);
                    }
                    else
                    {
                        string message = "Failed at " + fileInfo.Name + "\n" + e.ToString();
                        logger.Error(message);
                    }
                }
            }
            if (outputToStdout)
            {
                //ClassDatabaseFile2::Write requires Seek capability which stdout doesn't have.
                MemoryStream stream = new MemoryStream();
                using (BinaryWriter databaseStreamWriter = new BinaryWriter(stream))
                {
                    databaseFile.Write(databaseStreamWriter);
                    databaseStreamWriter.Flush();
                    byte[] buffer = stream.GetBuffer();
                    Console.OpenStandardOutput().Write(buffer, 0, (int)stream.Length);
                }
            }
            else
            {
                Stream databaseStream = new FileStream("behaviourdb.dat", FileMode.Create, FileAccess.Write, FileShare.None, 128, FileOptions.RandomAccess);
                using (BinaryWriter databaseStreamWriter = new BinaryWriter(databaseStream))
                {
                    databaseFile.Write(databaseStreamWriter);
                }
            }

            logger = null;
        }
	}
}
