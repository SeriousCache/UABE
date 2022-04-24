using System;
using System.IO;

namespace TypeTreeGenerator
{
	public class Logger
	{
		class PrependText : Attribute
		{
			public string prependText;
			public PrependText(string prependText)
			{
				this.prependText = prependText;
			}
		}
		public enum Level
		{
			[PrependText("INFO : ")]
			INFO,
			[PrependText("INFO : ")]
			KEYINFO,
			[PrependText("")]
			NONE,
			[PrependText("WARNING : ")]
			WARNING,
			[PrependText("ERROR : ")]
			ERROR,
		}

		public static string Level_ToString(Level level)
		{
			return ((PrependText)level.GetType().GetMember(level.ToString())[0].GetCustomAttributes(typeof(PrependText),false)[0]).prependText;
		}

		private int minLogLevel;
		private FileStream fileLogger;
		private StreamWriter fileWriter;
		private Logger parentLogger;
		public Logger(string logPath, Logger parentLogger, bool logToConsole)
			: this(logPath, parentLogger, !logToConsole ? -1 : (int)Level.INFO)
		{
		}
		public Logger(string logPath, Logger parentLogger, int minLogToConsole)
		{
			this.minLogLevel = minLogToConsole;
			fileLogger = null;
			fileWriter = null;
			this.parentLogger = parentLogger;
			if (logPath != null) {
				try {
					File.Delete(logPath);
					fileLogger = File.OpenWrite(logPath);
					fileWriter = new StreamWriter(fileLogger);
				} catch (Exception) {
					fileLogger = null;
					fileWriter = null;
					LogConsole(Level.WARNING, "Unable to open file '" + logPath + "' for writing!", true);
				}
			}
		}

		public void KeyInfo(string str = "")
		{
			Log(Level.KEYINFO, str);
		}
		public void Info(string str = "")
		{
			Log(Level.INFO, str);
		}
		public void Write(string str = "")
		{
			Log(Level.NONE, str);
		}
		public void Warning(string str = "")
		{
			Log(Level.WARNING, str);
		}
		public void Error(string str = "")
		{
			Log(Level.ERROR, str);
		}

		public void Log(Level level, string str = "")
		{
			LogConsole(level, str);
			try {
				LogFile(level, str);
			} catch (Exception e) {
				LogConsole(Level.WARNING, "An exception occured while writing to the log file :", true);
				LogConsole(Level.WARNING, e.ToString(), true);
			}
			if (parentLogger != null) {
				parentLogger.Log(level, str);
			}
		}
		public bool LogFile(Level level, string str)
		{
			str = Level_ToString(level) + str;
			if (fileWriter != null) {
				fileWriter.Write(str + "\r\n");
				fileWriter.Flush();
			}
			return fileWriter != null;
		}
		public void LogConsole(Level level, string str)
		{
			str = Level_ToString(level) + str;
			if ((int)level >= minLogLevel) {
				Console.WriteLine(str);
			}
		}
		private void LogConsole(Level level, string str, bool force)
		{
			str = Level_ToString(level) + str;
			if (force || ((int)level >= minLogLevel)) {
				Console.WriteLine(str);
			}
		}

		public void Close()
		{
			if (fileWriter != null)
				fileWriter.Close();
		}
	}
}

