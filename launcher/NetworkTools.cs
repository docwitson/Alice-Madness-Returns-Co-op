using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace AliceCoopLauncher
{
    internal static class NetworkTools
    {
        private const uint ProtocolMagic = 0x504F4341;
        private const ushort PingPacketType = 5;
        private const ushort PongPacketType = 6;

        public static ushort LocalProtocolVersion()
        {
            var server = Path.Combine(AppDomain.CurrentDomain.BaseDirectory,
                "AliceCoopServer.exe");
            if (!File.Exists(server))
                throw new FileNotFoundException(
                    "AliceCoopServer.exe is missing beside the launcher.", server);

            var process = Process.Start(new ProcessStartInfo {
                FileName = server,
                Arguments = "--protocol-version",
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true
            });
            if (process == null)
                throw new InvalidOperationException("Unable to query the protocol version.");
            using (process)
            {
                var stdout = process.StandardOutput.ReadToEnd();
                var stderr = process.StandardError.ReadToEnd();
                process.WaitForExit();
                if (process.ExitCode != 0 || stderr.Length != 0 ||
                    !Regex.IsMatch(stdout, "^\\d+\\r?\\n$", RegexOptions.CultureInvariant) ||
                    !ushort.TryParse(stdout.Trim(), NumberStyles.None,
                        CultureInfo.InvariantCulture, out var version))
                    throw new InvalidOperationException(
                        "AliceCoopServer.exe returned an invalid protocol version.");
                return version;
            }
        }

        public static IReadOnlyList<string> LocalAddresses()
        {
            var addresses = new List<Tuple<int, string>>();
            foreach (var network in NetworkInterface.GetAllNetworkInterfaces()
                .Where(item => item.OperationalStatus == OperationalStatus.Up &&
                    item.NetworkInterfaceType != NetworkInterfaceType.Loopback))
            {
                foreach (var address in network.GetIPProperties().UnicastAddresses)
                {
                    if (address.Address.AddressFamily == AddressFamily.InterNetwork &&
                        !IPAddress.IsLoopback(address.Address))
                    {
                        var classification = ClassifyAddress(address.Address, network.Name);
                        var suffixAlreadyPresent = network.Name.IndexOf(
                            classification.Item2, StringComparison.OrdinalIgnoreCase) >= 0;
                        addresses.Add(Tuple.Create(classification.Item1,
                            address.Address + " — " + network.Name +
                            (suffixAlreadyPresent ? string.Empty : " · " +
                                classification.Item2)));
                    }
                }
            }
            var ordered = addresses.OrderBy(item => item.Item1)
                .ThenBy(item => item.Item2, StringComparer.OrdinalIgnoreCase)
                .Select(item => item.Item2)
                .Distinct(StringComparer.OrdinalIgnoreCase)
                .ToList();
            if (ordered.Count != 0)
                ordered[0] += " · Recommended";
            return ordered;
        }

        private static Tuple<int, string> ClassifyAddress(IPAddress address,
            string adapterName)
        {
            var name = adapterName ?? string.Empty;
            var vpn = new[] { "radmin", "tailscale", "hamachi", "wireguard",
                "zerotier", "tunnel", "-tun", " tun", "vpn" };
            if (vpn.Any(marker => name.IndexOf(marker,
                StringComparison.OrdinalIgnoreCase) >= 0))
            {
                var priority = name.IndexOf("radmin",
                    StringComparison.OrdinalIgnoreCase) >= 0 ? 0 : 1;
                return Tuple.Create(priority, "VPN");
            }

            var bytes = address.GetAddressBytes();
            var privateLan = bytes[0] == 10 ||
                (bytes[0] == 172 && bytes[1] >= 16 && bytes[1] <= 31) ||
                (bytes[0] == 192 && bytes[1] == 168);
            if (privateLan)
                return Tuple.Create(2, "LAN");
            if (bytes[0] == 169 && bytes[1] == 254)
                return Tuple.Create(5, "Local only");
            return Tuple.Create(4, "Network");
        }

        public static byte[] BuildPingDatagram(ushort protocolVersion)
        {
            var bytes = new byte[24];
            Buffer.BlockCopy(BitConverter.GetBytes(ProtocolMagic), 0, bytes, 0, 4);
            Buffer.BlockCopy(BitConverter.GetBytes(protocolVersion), 0, bytes, 4, 2);
            Buffer.BlockCopy(BitConverter.GetBytes(PingPacketType), 0, bytes, 6, 2);
            Buffer.BlockCopy(BitConverter.GetBytes((uint)0), 0, bytes, 8, 4);
            Buffer.BlockCopy(BitConverter.GetBytes((uint)1), 0, bytes, 20, 4);
            return bytes;
        }

        public static async Task<bool> ProbeAsync(string address, int port,
            int timeoutMilliseconds = 1800)
        {
            IPAddress endpointAddress;
            if (!IPAddress.TryParse(address, out endpointAddress) ||
                endpointAddress.AddressFamily != AddressFamily.InterNetwork)
                return false;

            using (var client = new UdpClient(AddressFamily.InterNetwork))
            {
                var protocolVersion = LocalProtocolVersion();
                var bytes = BuildPingDatagram(protocolVersion);
                await client.SendAsync(bytes, bytes.Length,
                    new IPEndPoint(endpointAddress, port)).ConfigureAwait(false);
                var receive = client.ReceiveAsync();
                var timeout = Task.Delay(timeoutMilliseconds);
                var completed = await Task.WhenAny(receive, timeout).ConfigureAwait(false);
                if (completed != receive)
                    return false;
                var response = receive.Result.Buffer;
                return response.Length == 24 &&
                    BitConverter.ToUInt32(response, 0) == ProtocolMagic &&
                    BitConverter.ToUInt16(response, 4) == protocolVersion &&
                    BitConverter.ToUInt16(response, 6) == PongPacketType;
            }
        }

        public static string AddressOnly(string displayValue)
        {
            if (string.IsNullOrWhiteSpace(displayValue))
                return string.Empty;
            var separator = displayValue.IndexOf(" — ", StringComparison.Ordinal);
            return separator < 0 ? displayValue.Trim() :
                displayValue.Substring(0, separator).Trim();
        }
    }
}
