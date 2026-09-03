using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;

namespace AliceCoopLauncher
{
    internal sealed class LauncherSession : IDisposable
    {
        private readonly Mutex sessionMutex;
        private readonly string sessionPath;

        public LauncherSession()
        {
            var sessionId = Guid.NewGuid().ToString("N");
            MutexName = "Local\\AliceCoopLauncher-" + sessionId;
            sessionPath = Path.Combine(SessionsDirectory,
                "session-" + sessionId + ".ini");
            sessionMutex = new Mutex(true, MutexName);
        }

        public string MutexName { get; }

        public static string SettingsDirectory => Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "AliceCoop");

        public static string SessionsDirectory => Path.Combine(
            SettingsDirectory, "sessions");

        public string SessionPath => sessionPath;

        public static string PreferencesPath => Path.Combine(SettingsDirectory, "launcher.ini");

        public void Activate(string gameDirectory, string role,
            string serverAddress, int port, string displayMode)
        {
            Directory.CreateDirectory(SessionsDirectory);
            var text = new StringBuilder()
                .AppendLine("[Launcher]")
                .AppendLine("Active=1")
                .AppendLine("MutexName=" + MutexName)
                .AppendLine("GameDirectory=" + Path.GetFullPath(gameDirectory))
                .AppendLine()
                .AppendLine("[Network]")
                .AppendLine("Role=" + role)
                .AppendLine("ServerAddress=" + serverAddress)
                .AppendLine("Port=" + port)
                .AppendLine()
                .AppendLine("[Window]")
                .AppendLine("DisplayMode=" + displayMode)
                .ToString();
            WriteAtomically(sessionPath, text);
        }

        public static void SavePreferences(string gameDirectory,
            IEnumerable<string> gameDirectories, string serverAddress,
            int port, string displayMode)
        {
            Directory.CreateDirectory(SettingsDirectory);
            var directories = (gameDirectories ?? Enumerable.Empty<string>())
                .Where(item => !string.IsNullOrWhiteSpace(item))
                .Distinct(StringComparer.OrdinalIgnoreCase)
                .ToList();
            if (!string.IsNullOrWhiteSpace(gameDirectory) &&
                !directories.Contains(gameDirectory, StringComparer.OrdinalIgnoreCase))
                directories.Insert(0, gameDirectory);

            var text = new StringBuilder()
                .AppendLine("[Launcher]")
                .AppendLine("SettingsVersion=3")
                .AppendLine("GameDirectory=" + gameDirectory)
                .AppendLine("GameDirectoryCount=" + directories.Count)
                .AppendLine("ServerAddress=" + serverAddress)
                .AppendLine("Port=" + port)
                .AppendLine("DisplayMode=" + displayMode);
            for (var index = 0; index < directories.Count; ++index)
                text.AppendLine("GameDirectory" + index + "=" + directories[index]);
            WriteAtomically(PreferencesPath, text.ToString());
        }

        public static IReadOnlyList<string> ReadGameDirectories()
        {
            var results = new List<string>();
            if (int.TryParse(ReadPreference("GameDirectoryCount", "0"),
                out var count) && count > 0 && count < 100)
            {
                for (var index = 0; index < count; ++index)
                {
                    var value = ReadPreference("GameDirectory" + index, string.Empty);
                    if (!string.IsNullOrWhiteSpace(value))
                        results.Add(value);
                }
            }

            var legacy = ReadPreference("GameDirectory", string.Empty);
            if (!string.IsNullOrWhiteSpace(legacy) &&
                !results.Contains(legacy, StringComparer.OrdinalIgnoreCase))
                results.Insert(0, legacy);
            return results;
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
            try
            {
                if (File.Exists(sessionPath))
                    File.Delete(sessionPath);
            }
            catch (IOException)
            {
                // The mutex still makes an undeleted session file inactive.
            }
            sessionMutex.Dispose();
        }
    }
}
