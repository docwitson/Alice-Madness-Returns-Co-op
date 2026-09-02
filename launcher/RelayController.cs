using System;
using System.Diagnostics;
using System.IO;

namespace AliceCoopLauncher
{
    internal sealed class RelayController : IDisposable
    {
        private Process process;
        private bool stopRequested;

        public event Action<string> OutputReceived;
        public event Action<bool> Exited;

        public bool IsRunning => process != null && !process.HasExited;

        public void Start(string executable, int port, string logDirectory)
        {
            if (IsRunning)
                return;
            process?.Dispose();
            process = null;
            stopRequested = false;
            Directory.CreateDirectory(logDirectory);
            var info = new ProcessStartInfo {
                FileName = executable,
                Arguments = "--bind 0.0.0.0 --port " + port +
                    " --log-dir \"" + logDirectory + "\"",
                WorkingDirectory = Path.GetDirectoryName(executable),
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true
            };
            process = new Process { StartInfo = info, EnableRaisingEvents = true };
            process.OutputDataReceived += (_, e) => {
                if (!string.IsNullOrWhiteSpace(e.Data))
                    OutputReceived?.Invoke(e.Data);
            };
            process.ErrorDataReceived += (_, e) => {
                if (!string.IsNullOrWhiteSpace(e.Data))
                    OutputReceived?.Invoke(e.Data);
            };
            process.Exited += (_, __) => Exited?.Invoke(stopRequested);
            if (!process.Start())
                throw new InvalidOperationException("The relay process did not start.");
            process.BeginOutputReadLine();
            process.BeginErrorReadLine();
        }

        public void Stop()
        {
            if (!IsRunning)
                return;
            stopRequested = true;
            process.Kill();
            process.WaitForExit(3000);
        }

        public void Dispose()
        {
            Stop();
            process?.Dispose();
        }
    }
}
