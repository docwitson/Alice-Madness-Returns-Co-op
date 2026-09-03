using System;
using System.IO;
using System.Linq;

namespace AliceCoopLauncher
{
    internal static class LauncherSelfTest
    {
        public static bool Run()
        {
            try
            {
                var protocolVersion = NetworkTools.LocalProtocolVersion();
                var packet = NetworkTools.BuildPingDatagram(protocolVersion);
                return packet.Length == 24 &&
                    BitConverter.ToUInt32(packet, 0) == 0x504F4341 &&
                    BitConverter.ToUInt16(packet, 4) == protocolVersion &&
                    BitConverter.ToUInt16(packet, 6) == 5 &&
                    NetworkTools.LocalAddresses().Any(item =>
                        NetworkTools.AddressOnly(item) == "127.0.0.1") &&
                    GameLocator.PathsEqual(@"C:\Games\Alice\", @"c:\games\alice") &&
                    TestIsolatedSessions();
            }
            catch (Exception)
            {
                return false;
            }
        }

        private static bool TestIsolatedSessions()
        {
            var first = new LauncherSession();
            var second = new LauncherSession();
            try
            {
                var firstGame = Path.Combine(Path.GetTempPath(), "AliceCoopTestA");
                var secondGame = Path.Combine(Path.GetTempPath(), "AliceCoopTestB");
                first.Activate(firstGame, "host", "127.0.0.1", 27018, "windowed");
                second.Activate(secondGame, "client", "127.0.0.1", 27018, "windowed");
                return !string.Equals(first.SessionPath, second.SessionPath,
                        StringComparison.OrdinalIgnoreCase) &&
                    File.ReadAllText(first.SessionPath).Contains(
                        "GameDirectory=" + Path.GetFullPath(firstGame)) &&
                    File.ReadAllText(second.SessionPath).Contains(
                        "GameDirectory=" + Path.GetFullPath(secondGame));
            }
            finally
            {
                first.Dispose();
                second.Dispose();
            }
        }
    }
}
