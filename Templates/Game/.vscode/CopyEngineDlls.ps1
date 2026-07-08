# Copies required engine & dependency DLLs next to the standalone exe
# so the debugger can launch without PATH manipulation.
param(
    [Parameter(Mandatory=$true)]
    [string]$OutputDir,

    [Parameter(Mandatory=$true)]
    [string]$Config   # Debug or Release
)

$ErrorActionPreference = "Stop"

# Resolve ToolKit directory from the per-user config (same logic as root CMakeLists.txt)
$pathFile = "$env:APPDATA\ToolKit\Config\Path.txt"
if (-not (Test-Path $pathFile)) {
    Write-Error "Path.txt not found at $pathFile. Make sure ToolKit is installed."
    exit 1
}
$toolkitDir = (Get-Content $pathFile -First 1).Trim()
if (-not (Test-Path "$toolkitDir\ToolKit\Source\ToolKit.h")) {
    Write-Error "Invalid ToolKit directory: $toolkitDir"
    exit 1
}

$suffix = if ($Config -eq "Debug") { "d" } else { "" }

# Engine DLL (always needed). The engine ships one Bin folder per config
# (BinDebug / BinRelease), so index it by the requested $Config.
$toolkitBin  = "$toolkitDir\Bin$Config"
$depsDir     = "$toolkitDir\Dependency\Intermediate\Windows\$Config"

Write-Host "ToolKit: $toolkitDir"
Write-Host "Config : $Config"
Write-Host "Output : $OutputDir"

$dlls = @(
    "$toolkitBin\libToolKit$suffix.dll",
    "$toolkitBin\SDL2$suffix.dll",
    "$toolkitBin\imgui$suffix.dll"
)

foreach ($dll in $dlls) {
    if (Test-Path $dll) {
        Copy-Item $dll -Destination $OutputDir -Force
        $name = Split-Path $dll -Leaf
        Write-Host "  OK  $name"
    } else {
        Write-Warning "  MISSING  $dll"
    }
}

Write-Host "Done."
