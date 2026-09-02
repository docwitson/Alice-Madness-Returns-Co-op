using System;
using System.Windows;

namespace AliceCoopLauncher
{
    public partial class App : Application
    {
        protected override void OnStartup(StartupEventArgs e)
        {
            if (Array.Exists(e.Args, value =>
                string.Equals(value, "--self-test", StringComparison.OrdinalIgnoreCase)))
            {
                Environment.ExitCode = LauncherSelfTest.Run() ? 0 : 1;
                Shutdown(Environment.ExitCode);
                return;
            }

            var probeIndex = Array.FindIndex(e.Args, value =>
                string.Equals(value, "--probe", StringComparison.OrdinalIgnoreCase));
            if (probeIndex >= 0)
            {
                var port = 0;
                var valid = probeIndex + 2 < e.Args.Length &&
                    int.TryParse(e.Args[probeIndex + 2], out port) &&
                    port > 0 && port <= 65535;
                if (valid)
                    valid = NetworkTools.ProbeAsync(e.Args[probeIndex + 1], port)
                        .GetAwaiter().GetResult();
                Environment.ExitCode = valid ? 0 : 1;
                Shutdown(Environment.ExitCode);
                return;
            }

            base.OnStartup(e);
        }
    }
}
