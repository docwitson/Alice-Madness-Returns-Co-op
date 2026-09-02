using Microsoft.Win32;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;

namespace AliceCoopLauncher
{
    internal sealed class GameInstallation
    {
        public string Win32Directory { get; set; }
        public string Store { get; set; }
        public string SteamExecutable { get; set; }

        public override string ToString() => Store + " — " + Win32Directory;
    }

    internal static class GameLocator
    {
        public const string GameExecutableName = "AliceMadnessReturns.exe";
        public const int SteamAppId = 19680;

        public static IReadOnlyList<GameInstallation> FindInstallations()
        {
            var results = new List<GameInstallation>();
            AddSteamInstallations(results);
            AddInstalledLauncherDirectory(results);
            AddSavedDirectory(results);
            return results
                .Where(item => IsGameDirectory(item.Win32Directory))
                .GroupBy(item => Normalize(item.Win32Directory),
                    StringComparer.OrdinalIgnoreCase)
                .Select(group => group.First())
                .ToList();
        }

        private static void AddInstalledLauncherDirectory(List<GameInstallation> results)
        {
            var baseDirectory = AppDomain.CurrentDomain.BaseDirectory;
            var parent = Directory.GetParent(baseDirectory.TrimEnd(
                Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar));
            if (parent != null && IsGameDirectory(parent.FullName))
            {
                results.Add(new GameInstallation {
                    Win32Directory = parent.FullName,
                    Store = "Installed game"
                });
            }
        }

        private static void AddSavedDirectory(List<GameInstallation> results)
        {
            var saved = LauncherSession.ReadPreference("GameDirectory", string.Empty);
            if (IsGameDirectory(saved))
            {
                results.Add(new GameInstallation {
                    Win32Directory = saved,
                    Store = "Saved location"
                });
            }
        }

        private static void AddSteamInstallations(List<GameInstallation> results)
        {
            foreach (var steamRoot in SteamRoots())
            {
                var libraries = new HashSet<string>(StringComparer.OrdinalIgnoreCase) {
                    steamRoot
                };
                var libraryFile = Path.Combine(steamRoot, "steamapps", "libraryfolders.vdf");
                if (File.Exists(libraryFile))
                {
                    var text = File.ReadAllText(libraryFile);
                    foreach (Match match in Regex.Matches(text,
                        "\\\"path\\\"\\s+\\\"([^\\\"]+)\\\"",
                        RegexOptions.IgnoreCase))
                    {
                        libraries.Add(match.Groups[1].Value.Replace("\\\\", "\\"));
                    }
                }

                foreach (var library in libraries)
                {
                    var manifest = Path.Combine(library, "steamapps",
                        "appmanifest_" + SteamAppId + ".acf");
                    if (!File.Exists(manifest))
                        continue;
                    var match = Regex.Match(File.ReadAllText(manifest),
                        "\\\"installdir\\\"\\s+\\\"([^\\\"]+)\\\"",
                        RegexOptions.IgnoreCase);
                    if (!match.Success)
                        continue;
                    var win32 = Path.Combine(library, "steamapps", "common",
                        match.Groups[1].Value, "Binaries", "Win32");
                    results.Add(new GameInstallation {
                        Win32Directory = win32,
                        Store = "Steam",
                        SteamExecutable = Path.Combine(steamRoot, "steam.exe")
                    });
                }
            }
        }

        private static IEnumerable<string> SteamRoots()
        {
            var roots = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var pair in new[] {
                Tuple.Create(Registry.CurrentUser, @"Software\Valve\Steam", "SteamPath"),
                Tuple.Create(Registry.LocalMachine,
                    @"SOFTWARE\WOW6432Node\Valve\Steam", "InstallPath")
            })
            {
                try
                {
                    using (var key = pair.Item1.OpenSubKey(pair.Item2))
                    {
                        var value = key?.GetValue(pair.Item3) as string;
                        if (!string.IsNullOrWhiteSpace(value) && Directory.Exists(value))
                            roots.Add(Path.GetFullPath(value));
                    }
                }
                catch (Exception)
                {
                    // Store discovery is best-effort; manual selection remains available.
                }
            }
            return roots;
        }

        public static bool IsGameDirectory(string path) =>
            !string.IsNullOrWhiteSpace(path) &&
            File.Exists(Path.Combine(path, GameExecutableName));

        public static bool PathsEqual(string first, string second) =>
            string.Equals(Normalize(first), Normalize(second),
                StringComparison.OrdinalIgnoreCase);

        private static string Normalize(string path)
        {
            if (string.IsNullOrWhiteSpace(path))
                return string.Empty;
            return Path.GetFullPath(path).TrimEnd(
                Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        }

        public static GameInstallation Describe(string win32Directory)
        {
            var match = FindInstallations().FirstOrDefault(item =>
                PathsEqual(item.Win32Directory, win32Directory));
            return match ?? new GameInstallation {
                Win32Directory = win32Directory,
                Store = "Direct"
            };
        }

        public static Process Launch(GameInstallation installation)
        {
            if (string.Equals(installation.Store, "Steam",
                StringComparison.OrdinalIgnoreCase) &&
                File.Exists(installation.SteamExecutable))
            {
                return Process.Start(new ProcessStartInfo {
                    FileName = installation.SteamExecutable,
                    Arguments = "-applaunch " + SteamAppId,
                    UseShellExecute = true,
                    WorkingDirectory = Path.GetDirectoryName(installation.SteamExecutable)
                });
            }

            var executable = Path.Combine(installation.Win32Directory,
                GameExecutableName);
            return Process.Start(new ProcessStartInfo {
                FileName = executable,
                WorkingDirectory = installation.Win32Directory,
                UseShellExecute = true
            });
        }
    }
}
