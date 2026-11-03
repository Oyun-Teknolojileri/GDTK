#
# Measures the running insomnia app from the outside: present-to-present frame
# interval from SurfaceFlinger plus the process CPU share.
#
# Usage:  .\measure.ps1 -Label baseline [-Seconds 10]
#
param(
  [string]$Label = "run",
  [int]$Seconds = 10
)

$pkg = "com.otyazilim.toolkit.insomnia"

function Get-AppPid {
  $p = (adb shell pidof $pkg) -join ""
  if ([string]::IsNullOrWhiteSpace($p)) { return $null }
  return [int]($p.Trim())
}

function Get-SurfaceLayer {
  $list = adb shell dumpsys SurfaceFlinger --list
  $cand = $list | Where-Object { $_ -match "^SurfaceView - $([regex]::Escape($pkg))" }
  if ($cand -is [array]) { $cand = $cand[0] }
  return ($cand -replace "`0", "").Trim()
}

# Frame pacing: the latency buffer holds the last ~128 presents.
function Get-FrameStats {
  $layer = Get-SurfaceLayer
  if ([string]::IsNullOrEmpty($layer)) { return $null }
  $raw = adb shell "dumpsys SurfaceFlinger --latency '$layer'"
  $rows = $raw | Where-Object { $_ -match '^\d+\s+\d+\s+\d+' } | ForEach-Object {
    $p = ($_ -replace "`0", "") -split '\s+'
    [pscustomobject]@{ desired = [int64]$p[0]; actual = [int64]$p[1] }
  }
  # 0 / INT64_MAX rows are unused slots.
  $valid = $rows | Where-Object { $_.actual -gt 0 -and $_.actual -lt 9000000000000000000 } |
    Sort-Object actual
  if ($valid.Count -lt 8) { return $null }

  $t = @($valid | ForEach-Object { $_.actual })
  $d = for ($i = 1; $i -lt $t.Count; $i++) { ($t[$i] - $t[$i - 1]) / 1000000.0 }
  $d = @($d | Where-Object { $_ -gt 0.5 })   # drop duplicate presents in one vsync
  if ($d.Count -lt 5) { return $null }
  $sorted = $d | Sort-Object
  $mean = ($d | Measure-Object -Average).Average
  $median = $sorted[[int]($sorted.Count / 2)]
  return [pscustomobject]@{
    Frames   = $t.Count
    SpanMs   = ($t[-1] - $t[0]) / 1000000.0
    MeanMs   = $mean
    MedianMs = $median
    MinMs    = $sorted[0]
    MaxMs    = $sorted[-1]
    Fps      = 1000.0 / $mean
    FpsMed   = 1000.0 / $median
  }
}

$appPid = Get-AppPid
if ($null -eq $appPid) { Write-Error "app is not running"; exit 1 }

# CPU: /proc/<pid>/stat utime+stime in 100 Hz ticks.
$t0 = [int](((adb shell "cat /proc/$appPid/stat") -split '\s+')[13]) + [int](((adb shell "cat /proc/$appPid/stat") -split '\s+')[14])
$fs = Get-FrameStats
Start-Sleep -Seconds $Seconds
$fs2 = Get-FrameStats
$t1 = [int](((adb shell "cat /proc/$appPid/stat") -split '\s+')[13]) + [int](((adb shell "cat /proc/$appPid/stat") -split '\s+')[14])

$cpuPct = ($t1 - $t0) / $Seconds
$winRaw = (adb shell wm size) -join " "
$win = if ($winRaw -match 'Override size:\s*(\d+x\d+)') { $Matches[1] } else { "?" }

$r = if ($fs2) { $fs2 } else { $fs }
$line = "{0,-22} win={1,-10} fps={2,6:N2} (med {3,6:N2})  frame={4,7:N1}ms  min={5,6:N1}  max={6,6:N1}  cpu={7,5:N1}%  frames={8}" -f `
  $Label, $win, $r.Fps, $r.FpsMed, $r.MeanMs, $r.MinMs, $r.MaxMs, $cpuPct, $r.Frames
Write-Output $line
$line | Add-Content -Path (Join-Path $PSScriptRoot "results.txt")
