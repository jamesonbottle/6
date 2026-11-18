# GUI layout smoke test (Windows). Starts the GUI, waits for its window and checks what a user would see:
#   1. the tab control is the bottom-most child window (a tab control above the page controls hides the whole page),
#   2. a group box is below the controls inside it,
#   3. text is really drawn on the real screen where the page's labels must be (dark pixels in their rectangles).
# Usage: pwsh tools/gui_smoke.ps1 build\ddr5ramtest.exe        Exit 0 = pass, 1 = a check failed.
param([string]$Exe = "build\ddr5ramtest.exe")
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices; using System.Collections.Generic;
public class W {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c, string t);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr p, EnumProc f, IntPtr l);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  // Children from the TOP of the z-order to the bottom: "class|text|left|top|right|bottom"
  public static List<string> Children(IntPtr parent) {
    var rows = new List<string>();
    EnumChildWindows(parent, (h, l) => {
      var c = new StringBuilder(64); GetClassName(h, c, 64);
      var t = new StringBuilder(80); GetWindowText(h, t, 80);
      RECT r; GetWindowRect(h, out r);
      rows.Add(c + "|" + t + "|" + r.L + "|" + r.T + "|" + r.R + "|" + r.B); return true; }, IntPtr.Zero);
    return rows; }
}
"@

$failed = @()
function Check($ok, $what) { if ($ok) { "PASS  $what" } else { $script:failed += $what; "FAIL  $what" } }

$p = Start-Process (Resolve-Path $Exe) -PassThru
try {
    $h = [IntPtr]::Zero
    for ($i = 0; $i -lt 40 -and $h -eq [IntPtr]::Zero; $i++) {
        Start-Sleep -Milliseconds 500
        $h = [W]::FindWindow("Ddr5RamTestWindow", [NullString]::Value)
    }
    Check ($h -ne [IntPtr]::Zero) "the main window appears within 20 s"
    if ($h -eq [IntPtr]::Zero) { exit 1 }
    [void][W]::SetForegroundWindow($h)
    Start-Sleep -Seconds 2

    $kids = @([W]::Children($h) | ForEach-Object { $f = $_.Split("|"); [pscustomobject]@{ Cls = $f[0]; Text = $f[1]; L = [int]$f[2]; T = [int]$f[3]; R = [int]$f[4]; B = [int]$f[5] } })
    $kids | ForEach-Object { "  {0,-16} {1,-16} [{2},{3} {4}x{5}]" -f $_.Cls, $_.Text, $_.L, $_.T, ($_.R - $_.L), ($_.B - $_.T) }
    $tabAt = [array]::FindIndex($kids, [Predicate[object]]{ param($k) $k.Cls -eq "SysTabControl32" })
    Check ($tabAt -ge 0) "the tab control exists"
    Check ($tabAt -eq $kids.Count - 1) "the tab control is the bottom-most child (index $tabAt of $($kids.Count - 1) from the top)"
    $grp = [array]::FindIndex($kids, [Predicate[object]]{ param($k) $k.Text -eq "System status" })
    $lab = [array]::FindIndex($kids, [Predicate[object]]{ param($k) $k.Text -eq "Total memory:" })
    Check ($grp -ge 0 -and $lab -ge 0 -and $grp -gt $lab) "the 'System status' group box is below the label inside it"

    $r = New-Object W+RECT; [void][W]::GetWindowRect($h, [ref]$r)
    $bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
    $g = [System.Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size); $g.Dispose()
    foreach ($txt in @("Total memory:", "Duration:", "Pages verified:")) {
        $k = $kids | Where-Object { $_.Text -eq $txt } | Select-Object -First 1
        $dark = 0
        if ($k) {
            for ($x = $k.L - $r.L; $x -lt $k.R - $r.L; $x++) { for ($y = $k.T - $r.T; $y -lt $k.B - $r.T; $y++) {
                if ($x -ge 0 -and $y -ge 0 -and $x -lt $bmp.Width -and $y -lt $bmp.Height) {
                    $c = $bmp.GetPixel($x, $y); if (($c.R + $c.G + $c.B) / 3 -lt 110) { $dark++ } } } }
        }
        Check ($dark -gt 20) "'$txt' is drawn on the screen ($dark dark pixels)"
    }
    $bmp.Dispose()
}
finally { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
if ($failed.Count -gt 0) { "GUI smoke FAILED: " + ($failed -join "; "); exit 1 }
"GUI smoke PASSED"
