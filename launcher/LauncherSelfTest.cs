using System;

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
                    GameLocator.PathsEqual(@"C:\Games\Alice\", @"c:\games\alice");
            }
            catch (Exception)
            {
                return false;
            }
        }
    }
}
