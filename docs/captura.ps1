# Captura la ventana de la demo (la de demo-run\kciwapp2.exe) a un PNG, con
# PrintWindow: no importa que este tapada. Uso: captura.ps1 salida.png
param([string]$Salida = "captura.png")
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class W {
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out R r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [StructLayout(LayoutKind.Sequential)] public struct R { public int L, T, Rt, B; }
}
"@
[W]::SetProcessDPIAware() | Out-Null
$p = Get-Process kciwapp2 | Where-Object { $_.Path -like '*demo-run*' } | Select-Object -First 1
if (-not $p) { throw "demo no esta corriendo" }
$h = $p.MainWindowHandle
[W]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 400
$r = New-Object W+R
[W]::GetWindowRect($h, [ref]$r) | Out-Null
$w = $r.Rt - $r.L; $hh = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap $w, $hh
$g = [System.Drawing.Graphics]::FromImage($bmp)
$dc = $g.GetHdc()
[W]::PrintWindow($h, $dc, 2) | Out-Null   # PW_RENDERFULLCONTENT: tambien lo que dibuja DirectX
$g.ReleaseHdc($dc)
# Sin el marco invisible de Windows (8 px a los lados y abajo).
$rec = New-Object System.Drawing.Rectangle 8, 0, ($w - 16), ($hh - 8)
$bmp.Clone($rec, $bmp.PixelFormat).Save($Salida, [System.Drawing.Imaging.ImageFormat]::Png)
"$Salida $w x $hh"
