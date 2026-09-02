using System;
using System.IO;
using System.Text;
using System.Threading;

namespace AliceCoopLauncher
{
    internal sealed class LauncherSession : IDisposable
    {
        private readonly Mutex sessionMutex;

        public LauncherSession()
        {
            MutexName = "Local\\AliceCoopLauncher-" + Guid.NewGuid().ToString("N");
            sessionMutex = new Mutex(true, MutexName);
        }

        public string MutexName { get; }

        public static string SettingsDirectory => Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "AliceCoop");

        public static string SessionPath => Path.Combine(SettingsDirectory, "session.ini");

        public static string PreferencesPath => Path.Combine(SettingsDirectory, "launcher.ini");

        public void Activate(string role, string serverAddress, int port,
            string displayMode)
        {
            Directory.CreateDirectory(SettingsDirectory);
            var text = new StringBuilder()
                .AppendLine("[Launcher]")
                .AppendLine("Active=1")
                .AppendLine("MutexName=" + MutexName)
                .AppendLine()
                .AppendLine("[Network]")
                .AppendLine("Role=" + role)
                .AppendLine("ServerAddress=" + serverAddress)
                .AppendLine("Port=" + port)
                .AppendLine()
                .AppendLine("[Window]")
                .AppendLine("DisplayMode=" + displayMode)
                .ToString();
            WriteAtomically(SessionPath, text);
        }

        public static void SavePreferences(string gameDirectory, string serverAddress,
            int port, string displayMode)
        {
            Directory.CreateDirectory(SettingsDirectory);
            var text = new StringBuilder()
                .AppendLine("[Launcher]")
                .AppendLine("GameDirectory=" + gameDirectory)
                .AppendLine("ServerAddress=" + serverAddress)
                .AppendLine("Port=" + port)
                .AppendLine("DisplayMode=" + displayMode)
                .ToString();
            WriteAtomically(PreferencesPath, text);
        }

        public static string ReadPreference(string key, string fallback)
        {
            if (!File.Exists(PreferencesPath))
                return fallback;

            foreach (var rawLine in File.ReadAllLines(PreferencesPath))
            {
                var line = rawLine.Trim();
                var separator = line.IndexOf('=');
                if (separator <= 0)
                    continue;
                if (string.Equals(line.Substring(0, separator).Trim(), key,
                    StringComparison.OrdinalIgnoreCase))
                    return line.Substring(separator + 1).Trim();
            }
            return fallback;
        }

        private static void WriteAtomically(string path, string contents)
        {
            var temporaryPath = path + ".tmp";
            File.WriteAllText(temporaryPath, contents, Encoding.Unicode);
            if (File.Exists(path))
                File.Replace(temporaryPath, path, null);
            else
                File.Move(temporaryPath, path);
        }

        public void Dispose()
        {
            sessionMutex.Dispose();
        }
    }
}
