param(
    [Parameter(Mandatory)][ValidateRange(1,2147483647)][int]$ProcessId,
    [Parameter(Mandatory)][string]$DllDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$CaseLabel = 'snapshot'
)
$ErrorActionPreference = 'Stop'
$directory = (Resolve-Path -LiteralPath $DllDirectory).Path
$output = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $output -Force | Out-Null
if (-not ('LepInputSnapshot' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class LepInputSnapshot {
    public delegate bool EnumProc(IntPtr window, IntPtr data);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback, IntPtr data);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr parent, EnumProc callback, IntPtr data);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window, StringBuilder text, int count);
    [DllImport("user32.dll")] static extern bool IsWindowUnicode(IntPtr window);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] static extern IntPtr GetKeyboardLayout(uint thread);
    [DllImport("user32.dll")] public static extern int GetSystemMetrics(int index);
    [DllImport("kernel32.dll")] public static extern uint GetACP();
    [DllImport("kernel32.dll")] public static extern uint GetOEMCP();
    [DllImport("user32.dll")] static extern bool GetGUIThreadInfo(uint thread, ref GUI info);
    [StructLayout(LayoutKind.Sequential)] struct RECT { public int left, top, right, bottom; }
    [StructLayout(LayoutKind.Sequential)] struct GUI {
        public uint size, flags;
        public IntPtr active, focus, capture, menuOwner, moveSize, caret;
        public RECT caretRect;
    }
    public class Window {
        public string hwnd, parent, className, keyboardLayout, focusedWindow;
        public uint threadId;
        public bool nativeUnicode, visible, guiInfoAvailable;
    }
    static string Hex(IntPtr value) { return "0x" + unchecked((ulong)value.ToInt64()).ToString("X"); }
    public static Window[] Read(uint processId) {
        var windows = new List<Window>();
        var seen = new HashSet<IntPtr>();
        Action<IntPtr,IntPtr> add = (window,parent) => {
            uint process;
            uint thread = GetWindowThreadProcessId(window,out process);
            if (process != processId || !seen.Add(window)) return;
            var name = new StringBuilder(128);
            GetClassName(window,name,name.Capacity);
            GUI gui = new GUI(); gui.size = (uint)Marshal.SizeOf(typeof(GUI));
            bool ok = GetGUIThreadInfo(thread,ref gui);
            windows.Add(new Window {
                hwnd=Hex(window), parent=Hex(parent), className=name.ToString(), threadId=thread,
                nativeUnicode=IsWindowUnicode(window), visible=IsWindowVisible(window),
                keyboardLayout=Hex(GetKeyboardLayout(thread)), guiInfoAvailable=ok,
                focusedWindow=ok ? Hex(gui.focus) : null
            });
        };
        EnumWindows((window,data) => {
            uint process; GetWindowThreadProcessId(window,out process);
            if (process == processId) {
                add(window,IntPtr.Zero);
                EnumChildWindows(window,(child,unused) => { add(child,window); return true; },IntPtr.Zero);
            }
            return true;
        },IntPtr.Zero);
        return windows.ToArray();
    }
}
'@
}
$target = Get-Process -Id $ProcessId -ErrorAction Stop
$path = $null
try { $path = $target.Path } catch {}
$started = $null
try { $started = $target.StartTime.ToUniversalTime().ToString('o') } catch {}
$modules = @()
$moduleError = $null
try {
    $modules = @($target.Modules | Where-Object { $_.ModuleName -match '^(user32|imm32|msctf|win32u|kernelbase|gdi32|LocaleEmulatorPlus.*|LoaderDll.*)\.dll$' } | ForEach-Object {
        $hash = $null
        try { $hash = (Get-FileHash -LiteralPath $_.FileName -Algorithm SHA256).Hash } catch {}
        [pscustomobject]@{Name=$_.ModuleName;Path=$_.FileName;BaseAddress=('0x{0:X}' -f $_.BaseAddress.ToInt64());Version=$_.FileVersionInfo.FileVersion;SHA256=$hash}
    })
} catch { $moduleError = $_.Exception.Message }
$dlls = @(Get-ChildItem -LiteralPath $directory -File | Where-Object { $_.Name -match '^(LocaleEmulatorPlus|LoaderDll)_(x86|x64)\.dll$' } | ForEach-Object {
    [pscustomobject]@{Name=$_.Name;SHA256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
})
$record = [ordered]@{
    Case=$CaseLabel;Utc=[DateTime]::UtcNow.ToString('o');ProcessId=$ProcessId;ProcessName=$target.ProcessName
    Executable=$path;StartTimeUtc=$started
    ObserverBitness=[IntPtr]::Size*8
    HostObserver=[ordered]@{ACP=[LepInputSnapshot]::GetACP();OEMCP=[LepInputSnapshot]::GetOEMCP();SM_DBCSENABLED=[LepInputSnapshot]::GetSystemMetrics(42);OS=[Environment]::OSVersion.Version.ToString()}
    Windows=@([LepInputSnapshot]::Read([uint32]$ProcessId));Modules=$modules;ModuleReadError=$moduleError;PackageDlls=$dlls
    Notes=@('HostObserver is outside the game; it is not the game effective ACP/DBCS state.',
            'nativeUnicode is observed externally; a wrapped ANSI procedure can have this flag set.',
            'Window parent denotes the enumerated top-level root, not the immediate parent.',
            'A manually mapped LEP Core can be absent from the loader module list; inspect its PID log.')
}
$snapshot = Join-Path $output "$CaseLabel.$ProcessId.json"
$record | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $snapshot -Encoding UTF8
$hexId = '{0:X}' -f $ProcessId
$logDirectory = Join-Path $directory 'Log'
if (Test-Path -LiteralPath $logDirectory) {
    Get-ChildItem -LiteralPath $logDirectory -File | Where-Object { $_.Name -match "\.$hexId\.log\.txt$" } | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $output ($CaseLabel + '.' + $_.Name)) -Force
    }
}
Write-Output $snapshot
