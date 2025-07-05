@echo off
setlocal enabledelayedexpansion

set "script_dir=%~dp0"

set "source=%script_dir%Bin"
set "import_dest=%script_dir%Utils\Import"
set "packer_dest=%script_dir%Utils\Packer"

if /i "%1"=="debug" (
    set "postfix=d"
    if not exist "%import_dest%" mkdir "%import_dest%"
    if not exist "%packer_dest%" mkdir "%packer_dest%"
) else (
    set "postfix="
)

copy "%source%\ToolKit%postfix%.dll" "%packer_dest%\ToolKit%postfix%.dll"
copy "%source%\ToolKit%postfix%.dll" "%import_dest%\ToolKit%postfix%.dll"

