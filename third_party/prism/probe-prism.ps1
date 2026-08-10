# Enumerates every prism backend in registry (= priority) order with the two
# feature bits prism.cpp's selection gate depends on, the two braille bits its
# output dispatch depends on (SUPPORTS_BRAILLE / SUPPORTS_OUTPUT), plus what
# initialize() returns. Run it after building a new prism.dll, before shipping.
#
#   powershell -NoProfile -File third_party\prism\probe-prism.ps1
#   powershell -NoProfile -File third_party\prism\probe-prism.ps1 -DllPath <build>\prism.dll
#
# With no argument it reads the vendored dist DLL that `kdev apply` copies into
# the game's patches\ folder; -DllPath points it at any other build (a
# candidate from build_kotor_x86\, or the deployed copy in the install), which
# is how a new DLL gets checked before it is copied anywhere. initialize() is
# only called on backends already reporting themselves live, exactly as
# prism.cpp's BackendIsUsable / WalkForLiveBackend gate does, so no vendor
# client library is loaded for a reader that is not running.
#
# What a healthy run looks like: the reader you have running shows
# IS_SUPPORTED_AT_RUNTIME True with initialize rc=0; readers you do not have
# show False and are skipped; OneCore and SAPI show True (they are the
# no-screen-reader fallbacks); and the last line is "probe finished cleanly",
# which also proves prism_backend_free did not fault.

param(
    [string]$DllPath = (Join-Path (Split-Path $PSScriptRoot -Parent) "prism-dist\x86\prism.dll")
)

# The KOTOR prism.dll is 32-bit (the games are x86 processes), so the probe
# must run in a 32-bit PowerShell — a 64-bit process cannot load the DLL.
# Relaunch under the SysWOW64 host when needed.
if ([Environment]::Is64BitProcess) {
    $ps32 = Join-Path $env:WINDIR "SysWOW64\WindowsPowerShell\v1.0\powershell.exe"
    if (-not (Test-Path $ps32)) {
        Write-Output "ERROR: 32-bit PowerShell not found at $ps32; cannot probe an x86 prism.dll"
        exit 1
    }
    & $ps32 -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -DllPath $DllPath
    exit $LASTEXITCODE
}

$src = @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class P {
  [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
  public static extern IntPtr LoadLibraryW(string path);

  [DllImport("prism.dll", CallingConvention = CallingConvention.Cdecl)]
  public static extern IntPtr prism_init(IntPtr cfg);
  [DllImport("prism.dll", CallingConvention = CallingConvention.Cdecl)]
  public static extern IntPtr prism_registry_count(IntPtr ctx);
  [DllImport("prism.dll", CallingConvention = CallingConvention.Cdecl)]
  public static extern ulong prism_registry_id_at(IntPtr ctx, IntPtr i);
  [DllImport("prism.dll", CallingConvention = CallingConvention.Cdecl)]
  public static extern IntPtr prism_registry_name(IntPtr ctx, ulong id);
  [DllImport("prism.dll", CallingConvention = CallingConvention.Cdecl)]
  public static extern int prism_registry_priority(IntPtr ctx, ulong id);
  [DllImport("prism.dll", CallingConvention = CallingConvention.Cdecl)]
  public static extern IntPtr prism_registry_acquire(IntPtr ctx, ulong id);
  [DllImport("prism.dll", CallingConvention = CallingConvention.Cdecl)]
  public static extern IntPtr prism_registry_acquire_best(IntPtr ctx);
  [DllImport("prism.dll", CallingConvention = CallingConvention.Cdecl)]
  public static extern IntPtr prism_backend_name(IntPtr b);
  [DllImport("prism.dll", CallingConvention = CallingConvention.Cdecl)]
  public static extern ulong prism_backend_get_features(IntPtr b);
  [DllImport("prism.dll", CallingConvention = CallingConvention.Cdecl)]
  public static extern int prism_backend_initialize(IntPtr b);
  [DllImport("prism.dll", CallingConvention = CallingConvention.Cdecl)]
  public static extern void prism_backend_free(IntPtr b);

  public static string Utf8(IntPtr p) {
    if (p == IntPtr.Zero) return "(null)";
    int n = 0; while (Marshal.ReadByte(p, n) != 0) n++;
    var b = new byte[n]; Marshal.Copy(p, b, 0, n);
    return Encoding.UTF8.GetString(b);
  }
}
'@

Add-Type -TypeDefinition $src -Language CSharp

$dll = $DllPath
Write-Output "prism.dll: $dll (exists: $(Test-Path $dll))"
[void][P]::LoadLibraryW($dll)

$ctx = [P]::prism_init([IntPtr]::Zero)
Write-Output "ctx (NULL config, as the mod passes): $ctx"

# NB: PowerShell variable names are case-insensitive, so these must not collide
# with the $live / $speak results computed from them.
$bitRuntime = [uint64]1
$bitSpeak   = [uint64]4
$bitBraille = [uint64]16
$bitOutput  = [uint64]32

$count = [P]::prism_registry_count($ctx).ToInt64()
Write-Output "registered backends: $count (registry order = descending priority)"
Write-Output ""

for ($i = 0; $i -lt $count; $i++) {
  $id = [P]::prism_registry_id_at($ctx, [IntPtr]$i)
  if ($id -eq 0) { continue }
  $name = [P]::Utf8([P]::prism_registry_name($ctx, $id))
  $prio = [P]::prism_registry_priority($ctx, $id)
  $b = [P]::prism_registry_acquire($ctx, $id)
  if ($b -eq [IntPtr]::Zero) { Write-Output "$name (prio $prio): acquire returned NULL"; continue }
  $f = [P]::prism_backend_get_features($b)
  $live = (([uint64]$f -band [uint64]$bitRuntime) -ne 0)
  $speak = (([uint64]$f -band [uint64]$bitSpeak) -ne 0)
  $brl = (([uint64]$f -band [uint64]$bitBraille) -ne 0)
  $outp = (([uint64]$f -band [uint64]$bitOutput) -ne 0)
  Write-Output "  raw features: 0x$($f.ToString('X16'))  (type $($f.GetType().Name))"
  $initTxt = "not attempted (gate would skip)"
  if ($live -and $speak) { $initTxt = "initialize rc=$([P]::prism_backend_initialize($b))" }
  Write-Output "$name"
  Write-Output "  priority: $prio"
  Write-Output "  IS_SUPPORTED_AT_RUNTIME: $live"
  Write-Output "  SUPPORTS_SPEAK: $speak"
  Write-Output "  SUPPORTS_BRAILLE: $brl"
  Write-Output "  SUPPORTS_OUTPUT: $outp"
  Write-Output "  $initTxt"
  [P]::prism_backend_free($b)
}

Write-Output ""
$best = [P]::prism_registry_acquire_best($ctx)
if ($best -ne [IntPtr]::Zero) {
  $bf = [P]::prism_backend_get_features($best)
  Write-Output "acquire_best -> $([P]::Utf8([P]::prism_backend_name($best)))  live=$((($bf -band $bitRuntime) -ne 0))"
} else {
  Write-Output "acquire_best -> NULL"
}
Write-Output "probe finished cleanly"
