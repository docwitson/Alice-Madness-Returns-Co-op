using Microsoft.Win32;
using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Navigation;

namespace AliceCoopLauncher
{
    public partial class MainWindow : Window
    {
        private readonly LauncherSession launcherSession = new LauncherSession();
        private readonly RelayController relay = new RelayController();
        private string activeWin32Directory;

        public MainWindow()
        {
            InitializeComponent();
            relay.OutputReceived += line => Dispatcher.BeginInvoke(new Action(() =>
                ReceiveRelayLine(line)));
            relay.Exited += () => Dispatcher.BeginInvoke(new Action(() => {
                SessionStatusText.Text = "Relay stopped";
                SessionStatusText.Foreground = FindBrush("WarningBrush");
            }));
            Loaded += MainWindow_Loaded;
            Closing += MainWindow_Closing;
            StateChanged += (_, __) =>
                MaximizeButton.Content = WindowState == WindowState.Maximized ? "❐" : "□";
        }

        private void MainWindow_Loaded(object sender, RoutedEventArgs e)
        {
            var installations = GameLocator.FindInstallations();
            var preferred = LauncherSession.ReadPreference("GameDirectory", string.Empty);
            var selected = installations.FirstOrDefault(item =>
                GameLocator.PathsEqual(item.Win32Directory, preferred)) ??
                installations.FirstOrDefault();
            if (selected != null)
                GameDirectoryTextBox.Text = selected.Win32Directory;

            var currentPreferences = LauncherSession.ReadPreference(
                "SettingsVersion", string.Empty) == "2";
            ServerAddressTextBox.Text = currentPreferences
                ? LauncherSession.ReadPreference("ServerAddress", string.Empty)
                : string.Empty;
            PortTextBox.Text = LauncherSession.ReadPreference("Port", "27018");
            SelectDisplayMode(LauncherSession.ReadPreference("DisplayMode", "fullscreen"));

            var addresses = NetworkTools.LocalAddresses();
            foreach (var address in addresses)
                LocalAddressComboBox.Items.Add(address);
            if (LocalAddressComboBox.Items.Count == 0)
                LocalAddressComboBox.Items.Add("No active LAN/VPN IPv4 address found");
            LocalAddressComboBox.SelectedIndex = 0;

            RefreshInstallationState();
        }

        private void RefreshInstallationState()
        {
            var path = GameDirectoryTextBox.Text.Trim();
            var installed = GameLocator.IsGameDirectory(path) &&
                File.Exists(Path.Combine(path, "dinput8.dll")) &&
                File.Exists(Path.Combine(path, "AliceCoop", "AliceCoopServer.exe")) &&
                File.Exists(Path.Combine(path, "AliceCoop", "AliceCoopLauncher.exe"));
            activeWin32Directory = installed ? path : null;
            if (installed)
            {
                InstallationStatusText.Text = "Installation detected";
                InstallationStatusText.Foreground = FindBrush("SuccessBrush");
                InstallButton.Content = "Repair installation";
                InstallButton.ClearValue(StyleProperty);
            }
            else
            {
                InstallationStatusText.Text = PackageInstaller.IsPackageMode
                    ? "Select the folder containing AliceMadnessReturns.exe, then install."
                    : "Alice Co-op installation is incomplete. Run the launcher from the installer package.";
                InstallationStatusText.Foreground = FindBrush("WarningBrush");
                InstallButton.Content = "Install / Repair";
                InstallButton.Style = (Style)FindResource("PrimaryButton");
            }
            InstallButton.Visibility = PackageInstaller.IsPackageMode
                ? Visibility.Visible : Visibility.Collapsed;
        }

        private System.Windows.Media.Brush FindBrush(string name) =>
            (System.Windows.Media.Brush)FindResource(name);

        private void BrowseGame_Click(object sender, RoutedEventArgs e)
        {
            var dialog = new OpenFileDialog {
                Title = "Select AliceMadnessReturns.exe",
                Filter = "Alice: Madness Returns|AliceMadnessReturns.exe",
                CheckFileExists = true,
                FileName = GameLocator.GameExecutableName
            };
            if (dialog.ShowDialog(this) == true)
            {
                GameDirectoryTextBox.Text = Path.GetDirectoryName(dialog.FileName);
                RefreshInstallationState();
            }
        }

        private void Install_Click(object sender, RoutedEventArgs e)
        {
            var path = GameDirectoryTextBox.Text.Trim();
            if (!GameLocator.IsGameDirectory(path))
            {
                MessageBox.Show(this, "AliceMadnessReturns.exe was not found in that folder.",
                    "Alice Co-op", MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }
            try
            {
                InstallButton.IsEnabled = false;
                InstallationStatusText.Text = "Installing… approve the Windows prompt if it appears.";
                var exitCode = PackageInstaller.Install(path);
                if (exitCode != 0)
                    throw new InvalidOperationException("Installer exited with code " + exitCode + ".");
                RefreshInstallationState();
                AppendStatus("Installation completed and verified.");
            }
            catch (Exception exception)
            {
                MessageBox.Show(this, exception.Message, "Installation failed",
                    MessageBoxButton.OK, MessageBoxImage.Error);
                AppendStatus("Installation failed: " + exception.Message);
            }
            finally
            {
                InstallButton.IsEnabled = true;
            }
        }

        private async void Host_Click(object sender, RoutedEventArgs e)
        {
            if (!TryPrepare(out var port, out var displayMode))
                return;
            try
            {
                var coopDirectory = Path.Combine(activeWin32Directory, "AliceCoop");
                var server = Path.Combine(coopDirectory, "AliceCoopServer.exe");
                relay.Start(server, port, Path.Combine(coopDirectory, "logs"));
                SessionStatusText.Text = "Relay running — launching host";
                SessionStatusText.Foreground = FindBrush("SuccessBrush");
                await Task.Delay(250);
                if (!relay.IsRunning)
                    throw new InvalidOperationException(
                        "The relay stopped immediately. The UDP port may already be in use.");
                launcherSession.Activate("host", "127.0.0.1", port, displayMode);
                SavePreferences(port, displayMode);
                LaunchSelectedGame();
            }
            catch (Exception exception)
            {
                relay.Stop();
                AppendStatus("Host launch failed: " + exception.Message);
                MessageBox.Show(this, exception.Message, "Unable to host",
                    MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        private async void Join_Click(object sender, RoutedEventArgs e)
        {
            if (!TryPrepare(out var port, out var displayMode))
                return;
            if (!TryReadServerEndpoint(ref port, out var address))
                return;
            try
            {
                ProbeStatusText.Text = "Checking…";
                var reachable = await NetworkTools.ProbeAsync(address, port);
                ProbeStatusText.Text = reachable ? "Relay reachable ✓" : "No reply";
                ProbeStatusText.Foreground = FindBrush(reachable ? "SuccessBrush" : "WarningBrush");
                if (!reachable && MessageBox.Show(this,
                    "The relay did not reply. Check the address, VPN and host firewall. Start the game anyway?",
                    "Relay not reached", MessageBoxButton.YesNo, MessageBoxImage.Warning) !=
                    MessageBoxResult.Yes)
                    return;
                launcherSession.Activate("client", address, port, displayMode);
                SavePreferences(port, displayMode);
                SessionStatusText.Text = "Client launching — waiting to connect";
                LaunchSelectedGame();
            }
            catch (Exception exception)
            {
                AppendStatus("Client launch failed: " + exception.Message);
                MessageBox.Show(this, exception.Message, "Unable to join",
                    MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        private async void TestConnection_Click(object sender, RoutedEventArgs e)
        {
            var port = 0;
            if (!TryReadServerEndpoint(ref port, out var address))
                return;
            TestConnectionButton.IsEnabled = false;
            ProbeStatusText.Text = "Checking…";
            try
            {
                var success = await NetworkTools.ProbeAsync(
                    address, port);
                ProbeStatusText.Text = success ? "Relay reachable ✓" : "No reply";
                ProbeStatusText.Foreground = FindBrush(success ? "SuccessBrush" : "WarningBrush");
            }
            catch (Exception exception)
            {
                ProbeStatusText.Text = "Check failed";
                AppendStatus(exception.Message);
            }
            finally
            {
                TestConnectionButton.IsEnabled = true;
            }
        }

        private bool TryPrepare(out int port, out string displayMode,
            bool confirmRunningGame = true)
        {
            port = 0;
            displayMode = CurrentDisplayMode();
            RefreshInstallationState();
            if (activeWin32Directory == null)
            {
                MessageBox.Show(this, "Install Alice Co-op into the selected game first.",
                    "Installation required", MessageBoxButton.OK, MessageBoxImage.Warning);
                return false;
            }
            var runningGames = confirmRunningGame
                ? GameLocator.RunningGameExecutables()
                : Array.Empty<string>();
            if (runningGames.Count != 0)
            {
                var selectedExecutable = Path.Combine(activeWin32Directory,
                    GameLocator.GameExecutableName);
                var runningText = string.Join(Environment.NewLine,
                    runningGames.Select(item => "• " + item));
                var sameCopy = runningGames.Any(item =>
                    File.Exists(item) && GameLocator.PathsEqual(item, selectedExecutable));
                var warning = sameCopy
                    ? "The selected game copy is already running. Starting it again may fail, " +
                      "especially through Steam."
                    : "Another Alice: Madness Returns copy is already running.";
                var answer = MessageBox.Show(this,
                    warning + Environment.NewLine + Environment.NewLine +
                    "Currently running:" + Environment.NewLine + runningText +
                    Environment.NewLine + Environment.NewLine +
                    "Selected for this launch:" + Environment.NewLine +
                    selectedExecutable + Environment.NewLine + Environment.NewLine +
                    "Choose Yes only if you understand this and want to launch the selected " +
                    "copy anyway.",
                    "Launch another Alice instance?", MessageBoxButton.YesNo,
                    MessageBoxImage.Warning, MessageBoxResult.No);
                if (answer != MessageBoxResult.Yes)
                    return false;
            }
            return TryReadPort(out port);
        }

        private bool TryReadServerEndpoint(ref int port, out string address)
        {
            var value = ServerAddressTextBox.Text.Trim();
            address = value;
            var separator = value.LastIndexOf(':');
            if (separator > 0 && value.IndexOf(':') == separator)
            {
                address = value.Substring(0, separator).Trim();
                var portText = value.Substring(separator + 1).Trim();
                if (!int.TryParse(portText, out port) || port < 1024 || port > 65535)
                {
                    ShowInvalidEndpoint();
                    return false;
                }
                PortTextBox.Text = port.ToString();
                ServerAddressTextBox.Text = address;
            }
            else if (!TryReadPort(out port))
            {
                return false;
            }

            if (!IPAddress.TryParse(address, out var parsed) ||
                parsed.AddressFamily != AddressFamily.InterNetwork)
            {
                ShowInvalidEndpoint();
                return false;
            }
            return true;
        }

        private void ShowInvalidEndpoint()
        {
            MessageBox.Show(this, "Enter an IPv4 LAN/VPN address, optionally followed by :port.",
                "Invalid address", MessageBoxButton.OK, MessageBoxImage.Warning);
        }

        private bool TryReadPort(out int port)
        {
            if (!int.TryParse(PortTextBox.Text.Trim(), out port) || port < 1024 || port > 65535)
            {
                MessageBox.Show(this, "Enter a UDP port between 1024 and 65535.",
                    "Invalid port", MessageBoxButton.OK, MessageBoxImage.Warning);
                return false;
            }
            return true;
        }

        private void LaunchSelectedGame()
        {
            var installation = GameLocator.Describe(activeWin32Directory);
            GameLocator.Launch(installation);
            AppendStatus("Launch requested through " + installation.Store + ".");
        }

        private void ReceiveRelayLine(string line)
        {
            AppendStatus(line);
            if (line.Contains("HOST connected"))
                SessionStatusText.Text = "Host connected — waiting for second player";
            if (line.Contains("CLIENT connected"))
                SessionStatusText.Text = "Both players connected ✓";
        }

        private void AppendStatus(string message)
        {
            SessionLogTextBox.Visibility = Visibility.Visible;
            SessionLogTextBox.AppendText(message + Environment.NewLine);
            SessionLogTextBox.ScrollToEnd();
        }

        private void CopyAddress_Click(object sender, RoutedEventArgs e)
        {
            if (!TryReadPort(out var port))
                return;
            var address = NetworkTools.AddressOnly(LocalAddressComboBox.SelectedItem as string);
            if (IPAddress.TryParse(address, out _))
            {
                Clipboard.SetText(address + ":" + port);
                AppendStatus("Address copied: " + address + ":" + port);
            }
        }

        private void Firewall_Click(object sender, RoutedEventArgs e)
        {
            if (!TryPrepare(out var port, out _, confirmRunningGame: false))
                return;
            try
            {
                var server = Path.Combine(activeWin32Directory, "AliceCoop",
                    "AliceCoopServer.exe");
                var arguments = "advfirewall firewall add rule name=\"AliceCoop Relay\" " +
                    "dir=in action=allow program=\"" + server + "\" enable=yes " +
                    "profile=private protocol=UDP localport=" + port;
                var process = Process.Start(new ProcessStartInfo {
                    FileName = "netsh.exe",
                    Arguments = arguments,
                    Verb = "runas",
                    UseShellExecute = true,
                    WindowStyle = ProcessWindowStyle.Hidden
                });
                process?.WaitForExit();
                AppendStatus(process != null && process.ExitCode == 0
                    ? "Private-network Firewall rule added."
                    : "Firewall rule was not added.");
            }
            catch (Exception exception)
            {
                AppendStatus("Firewall setup failed: " + exception.Message);
            }
        }

        private void OpenLogs_Click(object sender, RoutedEventArgs e)
        {
            if (activeWin32Directory == null)
                return;
            var logs = Path.Combine(activeWin32Directory, "AliceCoop", "logs");
            Directory.CreateDirectory(logs);
            Process.Start(new ProcessStartInfo { FileName = logs, UseShellExecute = true });
        }

        private void StopRelay_Click(object sender, RoutedEventArgs e)
        {
            relay.Stop();
            AppendStatus("Relay stopped.");
        }

        private void SavePreferences(int port, string displayMode)
        {
            LauncherSession.SavePreferences(activeWin32Directory,
                ServerAddressTextBox.Text.Trim(), port, displayMode);
        }

        private string CurrentDisplayMode()
        {
            var item = DisplayModeComboBox.SelectedItem as ComboBoxItem;
            return item?.Tag as string ?? "fullscreen";
        }

        private void SelectDisplayMode(string mode)
        {
            foreach (ComboBoxItem item in DisplayModeComboBox.Items)
            {
                if (string.Equals(item.Tag as string, mode,
                    StringComparison.OrdinalIgnoreCase))
                {
                    DisplayModeComboBox.SelectedItem = item;
                    return;
                }
            }
        }

        private void Minimize_Click(object sender, RoutedEventArgs e) =>
            WindowState = WindowState.Minimized;

        private void Maximize_Click(object sender, RoutedEventArgs e) =>
            WindowState = WindowState == WindowState.Maximized
                ? WindowState.Normal : WindowState.Maximized;

        private void Close_Click(object sender, RoutedEventArgs e) => Close();

        private void OpenRepository_Click(object sender,
            RequestNavigateEventArgs e)
        {
            Process.Start(new ProcessStartInfo {
                FileName = e.Uri.AbsoluteUri,
                UseShellExecute = true
            });
            e.Handled = true;
        }

        private void MainWindow_Closing(object sender,
            System.ComponentModel.CancelEventArgs e)
        {
            if (relay.IsRunning && MessageBox.Show(this,
                "Stop the relay and close the launcher? The running game will remain open.",
                "Alice Co-op", MessageBoxButton.YesNo, MessageBoxImage.Question) !=
                MessageBoxResult.Yes)
            {
                e.Cancel = true;
                return;
            }
            relay.Dispose();
            launcherSession.Dispose();
        }
    }
}
