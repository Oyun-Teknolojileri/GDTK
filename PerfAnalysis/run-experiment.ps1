#
# One measurement run of the insomnia app on the attached device.
#
# The app is forced closed before and after the run: a phone left rendering for minutes heats up,
# throttles, and every later number is then measured on a different machine. Every run therefore
# starts cold and ends closed.
#
# Usage:
#   .\run-experiment.ps1 -Label baseline
#   .\run-experiment.ps1 -Label res50 -Set "res=50"
#   .\run-experiment.ps1 -Label noaa  -Set "msaa=0","hdr=0","ssao=0"
#   .\run-experiment.ps1 -Label small -Window 720x1480
#
param(
  [Parameter(Mandatory = $true)][string]$Label,
  [string[]]$Set = @(),
  [int]$Warmup = 14,
  [int]$Seconds = 20,
  [string]$Window = ""
)

$pkg    = "com.otyazilim.toolkit.insomnia"
$act    = "com.otyazilim.toolkit.insomnia/com.otyazilim.toolkit.ToolKitActivity"
$outDir = $PSScriptRoot
$knobs  = @("res", "msaa", "hdr", "ssao", "bloom", "tm", "cull", "batch", "perf", "prof")

function Get-AppPid {
  $p = (adb shell pidof $pkg) -join ""
  if ([string]::IsNullOrWhiteSpace($p)) { return $null }
  return [int]($p.Trim())
}

function Stop-App {
  adb shell am force-stop $pkg | Out-Null
  for ($i = 0; $i -lt 10; $i++) {
    Start-Sleep -Milliseconds 400
    if ($null -eq (Get-AppPid)) { return $true }
  }
  return $false
}

function Get-SurfaceLayer {
  $list = adb shell dumpsys SurfaceFlinger --list
  $cand = $list | Where-Object { $_ -match "^SurfaceView - $([regex]::Escape($pkg))" }
  if ($cand -is [array]) { $cand = $cand[0] }
  return ($cand -replace "`0", "").Trim()
}

function Get-FrameStats {
  $layer = Get-SurfaceLayer
  if ([string]::IsNullOrEmpty($layer)) { return $null }
  $raw  = adb shell "dumpsys SurfaceFlinger --latency '$layer'"
  $rows = $raw | Where-Object { $_ -match '^\d+\s+\d+\s+\d+' } | ForEach-Object {
    $p = ($_ -replace "`0", "") -split '\s+'
    [pscustomobject]@{ desired = [int64]$p[0]; actual = [int64]$p[1] }
  }
  $valid = $rows | Where-Object { $_.actual -gt 0 -and $_.actual -lt 9000000000000000000 } |
    Sort-Object actual
  if ($valid.Count -lt 8) { return $null }

  $t = @($valid | ForEach-Object { $_.actual })
  $d = for ($i = 1; $i -lt $t.Count; $i++) { ($t[$i] - $t[$i - 1]) / 1000000.0 }
  $d = @($d | Where-Object { $_ -gt 0.5 })
  if ($d.Count -lt 5) { return $null }
  $mean = ($d | Measure-Object -Average).Average
  return [pscustomobject]@{ Fps = 1000.0 / $mean; MeanMs = $mean; Frames = $t.Count }
}

# ---- device closed, then configured -----------------------------------------------------------------
if (-not (Stop-App)) { Write-Error "app would not stop"; exit 1 }

if ($Window -ne "") {
  adb shell wm size $Window | Out-Null
  Start-Sleep -Seconds 3
}

# Reset to -1 rather than an empty value: a property set to "" is refused by the property service on
# this Android version, and a stale knob from the previous run would silently change this one's
# measurement. Every knob treats -1 as "not set", so it is the neutral value.
foreach ($k in $knobs) { adb shell "setprop debug.tk.$k -1" | Out-Null }

# Comma separated entries in one argument are accepted as well: powershell -File passes the command
# line through without splitting on commas, so "-Set a=1,b=2" arrives as a single string.
foreach ($kv in ($Set | ForEach-Object { $_ -split ',' } | Where-Object { $_ -ne "" })) {
  $parts = $kv -split "=", 2
  adb shell "setprop debug.tk.$($parts[0].Trim()) '$($parts[1].Trim())'" | Out-Null
}

adb logcat -c
adb shell am start -n $act | Out-Null

# Scene load plus shader warmup. The probe skips its own first frames as well.
Start-Sleep -Seconds $Warmup

$appPid = Get-AppPid
if ($null -eq $appPid) { Write-Error "app did not start"; exit 1 }

$t0 = [int](((adb shell "cat /proc/$appPid/stat") -split '\s+')[13]) + [int](((adb shell "cat /proc/$appPid/stat") -split '\s+')[14])
$f1 = Get-FrameStats
Start-Sleep -Seconds $Seconds
$f2 = Get-FrameStats
$t1 = [int](((adb shell "cat /proc/$appPid/stat") -split '\s+')[13]) + [int](((adb shell "cat /proc/$appPid/stat") -split '\s+')[14])

$log = Join-Path $outDir "$Label.log"
adb logcat -d -s TK_LOG:V | Where-Object { $_ -match "PERF|PROF|GPU timer|Shader in compile" } | Set-Content $log

$perf = Get-Content $log | Where-Object { $_ -match "PERF fps" } | Select-Object -Last 3
$cfg  = Get-Content $log | Where-Object { $_ -match "PERF config" } | Select-Object -Last 1
$timer = Get-Content $log | Where-Object { $_ -match "GPU timer" } | Select-Object -Last 1

$closed = Stop-App
$winRaw = (adb shell wm size) -join " "
$win    = if ($winRaw -match 'Override size:\s*(\d+x\d+)') { $Matches[1] } else { "?" }

$r  = if ($f2) { $f2 } else { $f1 }
$cpu = ($t1 - $t0) / $Seconds

$line = "{0,-18} win={1,-10} sfFps={2,6:N2} frame={3,6:N1}ms cpu={4,5:N1}%" -f $Label, $win, $r.Fps, $r.MeanMs, $cpu
Write-Output $line
if ($timer) { Write-Output ("  " + ($timer -replace '^.*TK_LOG\s*:\s*', '')) }
if ($cfg)   { Write-Output ("  " + ($cfg   -replace '^.*TK_LOG\s*:\s*', '')) }
foreach ($p in $perf) { Write-Output ("  " + ($p -replace '^.*TK_LOG\s*:\s*', '')) }
Write-Output ("  log: $log  closed={0}" -f $closed)
$line | Add-Content -Path (Join-Path $outDir "results.txt")
$perf | ForEach-Object { ($_ -replace '^.*TK_LOG\s*:\s*', '') } | Add-Content -Path (Join-Path $outDir "results.txt")
