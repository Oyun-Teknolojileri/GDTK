# Fast, non hanging engine build.
#
# Two things made iterating painful and both are handled here:
#
# 1. A build killed half way through leaves an orphan cl.exe holding the sources and object files.
#    The next edit then fails with "unable to unlink" and the next build looks stuck. Stray
#    toolchain processes are killed before anything else happens.
# 2. Piping a Visual Studio build through a filter keeps the pipeline open while MSBuild
#    recompiles every translation unit that includes a broken header, so a failure looks like a
#    hang. The build writes to a log file instead, and only the error lines are printed.
#
# A watchdog kills the build after the timeout, so a stuck compile can not block the caller.
param(
    [string]$Config   = "Release",
    [string]$Target   = "ALL",
    [int]$TimeoutSec  = 900
)

$buildDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\Intermediate\Windows\$Config"))
$log      = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\Temp\build_$Config.log"))

New-Item -ItemType Directory -Force -Path (Split-Path $log) | Out-Null

# 1. Orphans from an earlier killed build.
$stray = Get-Process -Name 'cl', 'link', 'MSBuild', 'vctip' -ErrorAction SilentlyContinue
foreach ($process in $stray)
{
    Write-Host ("  killing stray {0} ({1})" -f $process.ProcessName, $process.Id)
    Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
}

# 2. Build to a log.
$arguments = @("--build", $buildDir, "--config", $Config)
# "ALL" means every target in the solution, which is what a Release build has to be: a targeted
# build relinks ToolKit and leaves the hosts behind, and a host with a stale ABI exits silently.
if ($Target -ne "ALL")
{
    $arguments += @("--target", $Target)
}

$process = Start-Process -FilePath "cmake" `
                         -ArgumentList $arguments `
                         -NoNewWindow `
                         -PassThru `
                         -RedirectStandardOutput $log `
                         -RedirectStandardError "$log.err"

$finished = $process.WaitForExit($TimeoutSec * 1000)
$process.Refresh()

if (-not $finished -or -not $process.HasExited)
{
    Write-Host ("BUILD TIMEOUT after {0}s, killing it" -f $TimeoutSec)
    Get-Process -Name 'cl', 'link', 'MSBuild' -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    exit 124
}

$lines = @()
foreach ($file in @($log, "$log.err"))
{
    if (Test-Path $file) { $lines += Get-Content $file }
}

$errors = $lines | Select-String -Pattern 'error C[0-9]{4}|error LNK[0-9]{4}|fatal error|CMake Error|MSB[0-9]{4}'
if ($errors)
{
    $errors | Select-Object -First 12 | ForEach-Object { $_.Line }
    Write-Host ("BUILD FAILED: {0} error lines" -f $errors.Count)
    exit 1
}

# The exit code of a redirected Start-Process is not reliable here, and MSBuild reports failures on
# its output, so the log is the source of truth. An empty log means the build never ran.
if ($lines.Count -eq 0)
{
    Write-Host "BUILD FAILED: no output, the build did not run"
    exit 1
}

$lines | Select-String -Pattern '\.vcxproj ->' | ForEach-Object { $_.Line.Trim() }
Write-Host "BUILD OK"
exit 0
