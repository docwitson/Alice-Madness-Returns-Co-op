using Microsoft.Win32;
using System;
using System.Collections.Generic;
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
        private readonly List<GameInstallation> installations =
            new List<GameInstallation>();
        private string activeWin32Directory;
        private bool isLoading = true;
        private bool closeForUninstall;

        public MainWindow()
        {
            InitializeComponent();
            relay.OutputReceived += line => Dispatcher.BeginInvoke(new Action(() =>
                ReceiveRelayLine(line)));
            relay.Exited += expected => Dispatcher.BeginInvoke(new Action(() =>
                RelayExited(expected)));
            Loaded += MainWindow_Loaded;
            Closing += MainWindow_Closing;
            StateChanged += (_, __) =>
                MaximizeButton.Content = WindowState == WindowState.Maximized ? "❐" : "□";
        }

        private void MainWindow_Loaded(object sender, RoutedEventArgs e)
        {
            var settingsVersion = LauncherSession.ReadPreference(
                "SettingsVersion", string.Empty);
            var currentPreferences = settingsVersion == "2" || settingsVersion == "3";
            ServerAddressTextBox.Text = currentPreferences
                ? LauncherSession.ReadPreference("ServerAddress", string.Empty)
                : string.Empty;
            PortTextBox.Text = LauncherSession.ReadPreference("Port", "27018");
            SelectDisplayMode(LauncherSession.ReadPreference(
                "DisplayMode", "fullscreen"));

            installations.AddRange(GameLocator.FindInstallations());
            foreach (var installation in installations)
                GameInstallationComboBox.Items.Add(installation);
            var preferred = LauncherSession.ReadPreference("GameDirectory", string.Empty);
            GameInstallationComboBox.SelectedItem = installations.FirstOrDefault(item =>
                GameLocator.PathsEqual(item.Win32Directory, preferred)) ??
                installations.FirstOrDefault();

            foreach (var address in NetworkTools.LocalAddresses())
                LocalAddressComboBox.Items.Add(address);
            LocalAddressComboBox.SelectedIndex = 0;

            isLoading = false;
            RefreshInstallationState();
            UpdateJoinActions();
            UpdateSessionControls();
            ShowPendingRemovalStatus();
        }

        private void ShowPendingRemovalStatus()
        {
            var statusPath = Path.Combine(LauncherSession.SettingsDirectory,
                "uninstaller-status.txt");
            if (!File.Exists(statusPath))
                return;
            try
            {
                var message = File.ReadAllText(statusPath).Trim();
                File.Delete(statusPath);
                if (string.IsNullOrWhiteSpace(message))
                    return;
                var failed = message.IndexOf("failed", StringComparison.OrdinalIgnoreCase) >= 0;
                AppendStatus(message, failed);
                if (failed)
                {
                    MessageBox.Show(this, message, "Alice Co-op removal failed",
                        MessageBoxButton.OK, MessageBoxImage.Error);
                }
            }
            catch (IOException)
            {
                // A just-started elevated uninstaller may still own the status file.
            }
        }

        private string SelectedGameDirectory =>
            (GameInstallationComboBox.SelectedItem as GameInstallation)?.Win32Directory;

        private void RefreshInstallationState()
        {
            var path = SelectedGameDirectory;
            var gameFound = GameLocator.IsGameDirectory(path);
            var installed = gameFound &&
                File.Exists(Path.Combine(path, "dinput8.dll")) &&
                File.Exists(Path.Combine(path, "AliceCoop", "AliceCoopServer.exe")) &&
                File.Exists(Path.Combine(path, "AliceCoop", "AliceCoopLauncher.exe"));
            activeWin32Directory = installed ? path : null;

            if (installed)
            {
                InstallationStatusText.Text = "Installed in selected game";
                InstallationStatusText.Foreground = FindBrush("SuccessBrush");
                InstallButton.Content = "Repair";
                InstallButton.ClearValue(StyleProperty);
            }
            else if (gameFound)
            {
                InstallationStatusText.Text = PackageInstaller.IsPackageMode
                    ? "Ready to install in selected game"
                    : "Not installed. Open the launcher from the installer package.";
                InstallationStatusText.Foreground = FindBrush("WarningBrush");
                InstallButton.Content = "Install";
                InstallButton.Style = (Style)FindResource("PrimaryButton");
            }
            else
            {
                InstallationStatusText.Text = "Add an Alice: Madness Returns installation";
                InstallationStatusText.Foreground = FindBrush("WarningBrush");
                InstallButton.Content = "Install";
                InstallButton.Style = (Style)FindResource("PrimaryButton");
            }

            InstallButton.Visibility = PackageInstaller.IsPackageMode
                ? Visibility.Visible : Visibility.Collapsed;
            InstallButton.IsEnabled = gameFound;
            RemoveButton.Visibility = installed && PackageInstaller.CanUninstall(path)
                ? Visibility.Visible : Visibility.Collapsed;
            HostButton.IsEnabled = installed && !relay.IsRunning;
            UpdateJoinActions();
        }

        private System.Windows.Media.Brush FindBrush(string name) =>
            (System.Windows.Media.Brush)FindResource(name);

        private void GameInstallationComboBox_SelectionChanged(object sender,
            SelectionChangedEventArgs e)
        {
            if (isLoading)
                return;
            RefreshInstallationState();
            SavePreferencesSilently();
        }

        private void BrowseGame_Click(object sender, RoutedEventArgs e)
        {
            var dialog = new OpenFileDialog {
                Title = "Select AliceMadnessReturns.exe",
                Filter = "Alice: Madness Returns|AliceMadnessReturns.exe",
                CheckFileExists = true,
                FileName = GameLocator.GameExecutableName
            };
            if (dialog.ShowDialog(this) != true)
                return;

            var path = Path.GetDirectoryName(dialog.FileName);
            var selected = installations.FirstOrDefault(item =>
                GameLocator.PathsEqual(item.Win32Directory, path));
            if (selected == null)
            {
                var detected = GameLocator.Describe(path);
                selected = new GameInstallation {
                    Win32Directory = path,
                    Store = detected.Store == "Direct" ? "Added game" : detected.Store,
                    SteamExecutable = detected.SteamExecutable
                };
                installations.Add(selected);
                GameInstallationComboBox.Items.Add(selected);
            }
            GameInstallationComboBox.SelectedItem = selected;
            RefreshInstallationState();
            SavePreferencesSilently();
        }

        private void Install_Click(object sender, RoutedEventArgs e)
        {
            var path = SelectedGameDirectory;
            if (!GameLocator.IsGameDirectory(path))
            {
                MessageBox.Show(this, "AliceMadnessReturns.exe was not found in that folder.",
                    "Alice Co-op", MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }
            try
            {
                InstallButton.IsEnabled = false;
                InstallationStatusText.Text =
                    "Installing… approve the Windows prompt if it appears.";
                var exitCode = PackageInstaller.Install(path);
                if (exitCode != 0)
                    throw new InvalidOperationException(
                        "Installer exited with code " + exitCode + ".");
                RefreshInstallationState();
                SavePreferencesSilently();
                AppendStatus("Installation completed and verified.");
            }
            catch (Exception exception)
            {
                MessageBox.Show(this, exception.Message, "Installation failed",
                    MessageBoxButton.OK, MessageBoxImage.Error);
                AppendStatus("Installation failed: " + exception.Message, true);
            }
            finally
            {
                InstallButton.IsEnabled = GameLocator.IsGameDirectory(
                    SelectedGameDirectory);
            }
        }

        private void Remove_Click(object sender, RoutedEventArgs e)
        {
            var path = SelectedGameDirectory;
            if (string.IsNullOrWhiteSpace(path) || !PackageInstaller.CanUninstall(path))
                return;
            if (relay.IsRunning)
            {
                MessageBox.Show(this, "Stop hosting before removing Alice Co-op.",
                    "Alice Co-op", MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }
            if (GameLocator.RunningGameExecutables().Count != 0)
            {
                MessageBox.Show(this, "Close every running Alice game before removing the mod.",
                    "Alice Co-op", MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }
            var answer = MessageBox.Show(this,
                "Remove Alice Co-op from this game?\n\n" + path +
                "\n\nThe previous dinput8.dll will be restored when available. " +
                "Logs, client saves and recovery files will be preserved.",
                "Remove Alice Co-op", MessageBoxButton.YesNo,
                MessageBoxImage.Warning, MessageBoxResult.No);
            if (answer != MessageBoxResult.Yes)
                return;
            try
            {
                PackageInstaller.StartUninstall(path, Process.GetCurrentProcess().Id);
                closeForUninstall = true;
                Close();
            }
            catch (Exception exception)
            {
                MessageBox.Show(this, exception.Message, "Removal failed to start",
                    MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        private async void Host_Click(object sender, RoutedEventArgs e)
        {
            if (!TryPrepare(out var displayMode) || !TryReadPort(out var port))
                return;
            try
            {
                ShowSessionDetails();
                SessionStatusText.Text = "Starting host…";
                SessionStatusText.Foreground = FindBrush("SuccessBrush");
                var coopDirectory = Path.Combine(activeWin32Directory, "AliceCoop");
                relay.Start(Path.Combine(coopDirectory, "AliceCoopServer.exe"), port,
                    Path.Combine(coopDirectory, "logs"));
                UpdateSessionControls();
                await Task.Delay(250);
                if (!relay.IsRunning)
                    throw new InvalidOperationException(
                        "The host service stopped immediately. The UDP port may already be in use.");
                launcherSession.Activate("host", "127.0.0.1", port, displayMode);
                SavePreferences(port, displayMode);
                LaunchSelectedGame();
                SessionStatusText.Text = "Hosting — waiting for the game";
            }
            catch (Exception exception)
            {
                relay.Stop();
                SessionStatusText.Text = "Unable to host";
                SessionStatusText.Foreground = FindBrush("WarningBrush");
                AppendStatus("Host launch failed: " + exception.Message, true);
                MessageBox.Show(this, exception.Message, "Unable to host",
                    MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        private async void Join_Click(object sender, RoutedEventArgs e)
        {
            if (!TryPrepare(out var displayMode))
                return;
            var port = ReadPortOrDefault();
            if (!TryReadServerEndpoint(ref port, out var address))
                return;
            try
            {
                SessionStatusText.Text = "Checking host…";
                SessionStatusText.Foreground = FindBrush("SuccessBrush");
                ProbeStatusText.Text = "Checking…";
                var reachable = await NetworkTools.ProbeAsync(address, port);
                ProbeStatusText.Text = reachable ? "Host reachable ✓" : "No reply";
                ProbeStatusText.Foreground = FindBrush(
                    reachable ? "SuccessBrush" : "WarningBrush");
                if (!reachable && MessageBox.Show(this,
                    "The host did not reply. Check the address, VPN and host firewall. " +
                    "Start the game anyway?", "Host not reached",
                    MessageBoxButton.YesNo, MessageBoxImage.Warning) != MessageBoxResult.Yes)
                {
                    SessionStatusText.Text = "Ready";
                    return;
                }
                launcherSession.Activate("client", address, port, displayMode);
                SavePreferences(port, displayMode);
                LaunchSelectedGame();
                SessionStatusText.Text = "Client launched — waiting to connect";
            }
            catch (Exception exception)
            {
                SessionStatusText.Text = "Unable to join";
                SessionStatusText.Foreground = FindBrush("WarningBrush");
                AppendStatus("Client launch failed: " + exception.Message, true);
                MessageBox.Show(this, exception.Message, "Unable to join",
                    MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        private async void TestConnection_Click(object sender, RoutedEventArgs e)
        {
            var port = ReadPortOrDefault();
            if (!TryReadServerEndpoint(ref port, out var address))
                return;
            TestConnectionButton.IsEnabled = false;
            ProbeStatusText.Text = "Checking…";
            try
            {
                var success = await NetworkTools.ProbeAsync(address, port);
                ProbeStatusText.Text = success ? "Host reachable ✓" : "No reply";
                ProbeStatusText.Foreground = FindBrush(
                    success ? "SuccessBrush" : "WarningBrush");
            }
            catch (Exception exception)
            {
                ProbeStatusText.Text = "Check failed";
                AppendStatus(exception.Message, true);
            }
            finally
            {
                UpdateJoinActions();
            }
        }

        private bool TryPrepare(out string displayMode, bool confirmRunningGame = true)
        {
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
            if (runningGames.Count == 0)
                return true;

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
                "Selected for this launch:" + Environment.NewLine + selectedExecutable +
                Environment.NewLine + Environment.NewLine +
                "Choose Yes only if you understand this and want to launch the selected " +
                "copy anyway.", "Launch another Alice instance?",
                MessageBoxButton.YesNo, MessageBoxImage.Warning, MessageBoxResult.No);
            return answer == MessageBoxResult.Yes;
        }

        private bool TryReadServerEndpoint(ref int port, out string address)
        {
            var value = ServerAddressTextBox.Text.Trim();
            if (!TryParseServerEndpoint(value, port, out address, out var parsedPort))
            {
                ShowInvalidEndpoint();
                return false;
            }
            port = parsedPort;
            PortTextBox.Text = port.ToString();
            ServerAddressTextBox.Text = address;
            return true;
        }

        private static bool TryParseServerEndpoint(string value, int defaultPort,
            out string address, out int port)
        {
            address = (value ?? string.Empty).Trim();
            port = defaultPort;
            var separator = address.LastIndexOf(':');
            if (separator > 0 && address.IndexOf(':') == separator)
            {
                var portText = address.Substring(separator + 1).Trim();
                address = address.Substring(0, separator).Trim();
                if (!int.TryParse(portText, out port))
                    return false;
            }
            if (port < 1024 || port > 65535)
                return false;
            return IPAddress.TryParse(address, out var parsed) &&
                parsed.AddressFamily == AddressFamily.InterNetwork;
        }

        private void ShowInvalidEndpoint()
        {
            MessageBox.Show(this,
                "Enter the host IPv4 address, optionally followed by :port.",
                "Invalid address", MessageBoxButton.OK, MessageBoxImage.Warning);
        }

        private int ReadPortOrDefault() =>
            int.TryParse(PortTextBox.Text.Trim(), out var port) ? port : 27018;

        private bool TryReadPort(out int port)
        {
            if (!int.TryParse(PortTextBox.Text.Trim(), out port) ||
                port < 1024 || port > 65535)
            {
                MessageBox.Show(this, "Enter a UDP port between 1024 and 65535.",
                    "Invalid port", MessageBoxButton.OK, MessageBoxImage.Warning);
                return false;
            }
            return true;
        }

        private void ServerAddressTextBox_TextChanged(object sender,
            TextChangedEventArgs e) => UpdateJoinActions();

        private void PortTextBox_TextChanged(object sender,
            TextChangedEventArgs e) => UpdateJoinActions();

        private void UpdateJoinActions()
        {
            if (ServerAddressTextBox == null || HostAddressPlaceholder == null)
                return;
            var value = ServerAddressTextBox.Text.Trim();
            HostAddressPlaceholder.Visibility = value.Length == 0
                ? Visibility.Visible : Visibility.Collapsed;
            var valid = TryParseServerEndpoint(value, ReadPortOrDefault(),
                out _, out _);
            var installed = activeWin32Directory != null;
            JoinButton.IsEnabled = valid && installed;
            TestConnectionButton.IsEnabled = valid;
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
                SessionStatusText.Text = "Hosting — waiting for second player";
            if (line.Contains("CLIENT connected"))
                SessionStatusText.Text = "Both players connected ✓";
            SessionStatusText.Foreground = FindBrush("SuccessBrush");
        }

        private void RelayExited(bool expected)
        {
            if (!expected)
            {
                SessionStatusText.Text = "Hosting stopped unexpectedly";
                SessionStatusText.Foreground = FindBrush("WarningBrush");
                AppendStatus("The host service stopped unexpectedly.", true);
            }
            else if (!SessionStatusText.Text.StartsWith("Unable to host",
                StringComparison.Ordinal))
            {
                SessionStatusText.Text = "Ready";
                SessionStatusText.Foreground = FindBrush("SuccessBrush");
            }
            UpdateSessionControls();
        }

        private void AppendStatus(string message, bool revealDetails = false)
        {
            SessionLogTextBox.AppendText(message + Environment.NewLine);
            SessionLogTextBox.ScrollToEnd();
            if (revealDetails)
                ShowSessionDetails();
        }

        private void ShowSessionDetails()
        {
            SessionLogTextBox.Visibility = Visibility.Visible;
            SessionDetailsButton.Content = "Hide details";
        }

        private void ToggleSessionDetails_Click(object sender, RoutedEventArgs e)
        {
            var show = SessionLogTextBox.Visibility != Visibility.Visible;
            SessionLogTextBox.Visibility = show ? Visibility.Visible : Visibility.Collapsed;
            SessionDetailsButton.Content = show ? "Hide details" : "Show details";
        }

        private void UpdateSessionControls()
        {
            var running = relay.IsRunning;
            StopHostingButton.Visibility = running
                ? Visibility.Visible : Visibility.Collapsed;
            HostButton.IsEnabled = activeWin32Directory != null && !running;
        }

        private void LocalAddressComboBox_SelectionChanged(object sender,
            SelectionChangedEventArgs e)
        {
            if (RecommendedAddressText == null)
                return;
            var address = NetworkTools.AddressOnly(
                LocalAddressComboBox.SelectedItem as string);
            RecommendedAddressText.Visibility = LocalAddressComboBox.SelectedIndex == 0 &&
                address != "127.0.0.1" ? Visibility.Visible : Visibility.Hidden;
        }

        private void CopyAddress_Click(object sender, RoutedEventArgs e)
        {
            if (!TryReadPort(out var port))
                return;
            var address = NetworkTools.AddressOnly(
                LocalAddressComboBox.SelectedItem as string);
            if (IPAddress.TryParse(address, out _))
            {
                Clipboard.SetText(address + ":" + port);
                AppendStatus("Address copied: " + address + ":" + port);
            }
        }

        private void Firewall_Click(object sender, RoutedEventArgs e)
        {
            if (!TryPrepare(out _, confirmRunningGame: false) ||
                !TryReadPort(out var port))
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
                    : "Firewall rule was not added.",
                    process == null || process.ExitCode != 0);
            }
            catch (Exception exception)
            {
                AppendStatus("Firewall setup failed: " + exception.Message, true);
            }
        }

        private void OpenLogs_Click(object sender, RoutedEventArgs e)
        {
            var path = SelectedGameDirectory;
            if (string.IsNullOrWhiteSpace(path))
                return;
            var logs = Path.Combine(path, "AliceCoop", "logs");
            Directory.CreateDirectory(logs);
            Process.Start(new ProcessStartInfo { FileName = logs, UseShellExecute = true });
        }

        private void StopRelay_Click(object sender, RoutedEventArgs e)
        {
            relay.Stop();
            SessionStatusText.Text = "Ready";
            SessionStatusText.Foreground = FindBrush("SuccessBrush");
            AppendStatus("Hosting stopped.");
            UpdateSessionControls();
        }

        private void SavePreferences(int port, string displayMode)
        {
            LauncherSession.SavePreferences(SelectedGameDirectory,
                installations.Select(item => item.Win32Directory),
                ServerAddressTextBox.Text.Trim(), port, displayMode);
        }

        private void SavePreferencesSilently()
        {
            if (isLoading)
                return;
            SavePreferences(ReadPortOrDefault(), CurrentDisplayMode());
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
            if (!closeForUninstall && relay.IsRunning && MessageBox.Show(this,
                "Stop hosting and close the launcher? The running game will remain open.",
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
