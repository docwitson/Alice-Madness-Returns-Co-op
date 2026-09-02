using System;
using System.Diagnostics;
using System.IO;

namespace AliceCoopLauncher
{
    internal static class PackageInstaller
    {
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

        public static bool CanUninstall(string win32Directory) =>
            File.Exists(UninstallScript(win32Directory));

        public static void StartUninstall(string win32Directory, int launcherProcessId)
        {
            var script = UninstallScript(win32Directory);
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

        private static string UninstallScript(string win32Directory) =>
            Path.Combine(win32Directory, "AliceCoop", "Advanced", "Tools",
                "Uninstall-AliceCoop.ps1");

        private static string QuotePowerShell(string value) =>
            value.Replace("'", "''");
    }
}
