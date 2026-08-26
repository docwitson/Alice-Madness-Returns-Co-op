$ErrorActionPreference = 'Stop'

$nativeMethods = @'
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool SetProcessDPIAware();

[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern int GetSystemMetrics(int index);
'@

Add-Type -Name NativeDisplay -Namespace AliceCoop -MemberDefinition $nativeMethods
[void][AliceCoop.NativeDisplay]::SetProcessDPIAware()

# SM_CXSCREEN. Calling SetProcessDPIAware first prevents Windows scaling from
# reporting a 3840-pixel display as 2560 logical pixels at 150% UI scaling.
[AliceCoop.NativeDisplay]::GetSystemMetrics(0)
