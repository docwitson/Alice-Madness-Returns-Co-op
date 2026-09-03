using System;
using System.Diagnostics;
using System.IO;
using System.Text.RegularExpressions;

namespace AliceCoopLauncher
{
    internal static class PackageInstaller
    {
        public static string ServerExecutablePath => Path.Combine(
            AppDomain.CurrentDomain.BaseDirectory, "AliceCoopServer.exe");

        public static string PackageVersion
        {
            get
            {
                var manifest = Path.Combine(AppDomain.CurrentDomain.BaseDirectory,
                    "Advanced", "package-manifest.json");
                if (!File.Exists(manifest))
                    return "Development build";
                try
                {
                    var match = Regex.Match(File.ReadAllText(manifest),
                        "\\\"version\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"",
                        RegexOptions.IgnoreCase);
                    return match.Success ? match.Groups[1].Value : "Unknown version";
                }
                catch (IOException)
                {
                    return "Unknown version";
                }
            }
        }

        public static bool IsPackageMode
        {
            get
            {
                var script = Path.Combine(AppDomain.CurrentDomain.BaseDirectory,
                    "Advanced", "Tools", "Install-AliceCoop-Package.ps1");
                var payload = Path.Combine(AppDomain.CurrentDomain.BaseDirectory,
                    "Advanced", "Payload", "dinput8.dll");
                return File.Exists(script) && File.Exists(payload);
            }
        }

        public static int Install(string win32Directory)
        {
            var script = Path.Combine(AppDomain.CurrentDomain.BaseDirectory,
                "Advanced", "Tools", "Install-AliceCoop-Package.ps1");
            if (!File.Exists(script))
                throw new FileNotFoundException("The installer support files are missing.", script);

            Directory.CreateDirectory(LauncherSession.SettingsDirectory);
            var statusPath = Path.Combine(LauncherSession.SettingsDirectory,
                "installer-status.txt");
            if (File.Exists(statusPath))
                File.Delete(statusPath);

            var arguments = "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden " +
                "-File \"" + script + "\" -Win32Path \"" + win32Directory +
                "\" -StatusPath \"" + statusPath + "\"";
            var process = Process.Start(new ProcessStartInfo {
                FileName = "powershell.exe",
                Arguments = arguments,
                UseShellExecute = true,
                Verb = "runas",
                WindowStyle = ProcessWindowStyle.Hidden
            });
            if (process == null)
                throw new InvalidOperationException("The installer process did not start.");
            process.WaitForExit();
            if (process.ExitCode != 0 && File.Exists(statusPath))
            {
                var details = File.ReadAllText(statusPath).Trim();
                if (!string.IsNullOrWhiteSpace(details))
                    throw new InvalidOperationException(details);
            }
            return process.ExitCode;
        }

        public static bool IsPayloadInstalled(string win32Directory) =>
            !string.IsNullOrWhiteSpace(win32Directory) &&
            File.Exists(Path.Combine(win32Directory, "dinput8.dll")) &&
            File.Exists(Path.Combine(win32Directory, "AliceCoop", "AliceCoop.ini")) &&
            File.Exists(Path.Combine(win32Directory, "AliceCoop", "images",
                "aliceWhait.png")) &&
            (File.Exists(Path.Combine(win32Directory, "AliceCoop",
                "install-manifest.json")) ||
             File.Exists(Path.Combine(win32Directory, "AliceCoop", "Advanced",
                "package-manifest.json")));

        public static bool CanUninstall(string win32Directory) =>
            IsPayloadInstalled(win32Directory) && File.Exists(UninstallScript());

        public static void StartUninstall(string win32Directory, int launcherProcessId)
        {
            var script = UninstallScript();
            if (!File.Exists(script))
                throw new FileNotFoundException("The uninstall support files are missing.", script);

            Directory.CreateDirectory(LauncherSession.SettingsDirectory);
            var statusPath = Path.Combine(LauncherSession.SettingsDirectory,
                "uninstaller-status.txt");
            if (File.Exists(statusPath))
                File.Delete(statusPath);

            var command = "& { Wait-Process -Id " + launcherProcessId +
                " -ErrorAction SilentlyContinue; & '" + QuotePowerShell(script) +
                "' -Win32Path '" + QuotePowerShell(win32Directory) +
                "' -StatusPath '" + QuotePowerShell(statusPath) +
                "'; exit 0 }";
            var process = Process.Start(new ProcessStartInfo {
                FileName = "powershell.exe",
                Arguments = "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden " +
                    "-Command \"" + command.Replace("\"", "`\"") + "\"",
                UseShellExecute = true,
                Verb = "runas",
                WindowStyle = ProcessWindowStyle.Hidden
            });
            if (process == null)
                throw new InvalidOperationException("The uninstaller process did not start.");
        }

        private static string UninstallScript() =>
            Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Advanced", "Tools",
                "Uninstall-AliceCoop.ps1");

        private static string QuotePowerShell(string value) =>
            value.Replace("'", "''");
    }
}
