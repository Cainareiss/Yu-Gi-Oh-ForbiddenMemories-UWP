param(
    [Parameter(Mandatory = $true)][string]$LinkTool,
    [Parameter(Mandatory = $true)][string]$Output,
    [Parameter(Mandatory = $true)][string]$ObjectDirectory,
    [Parameter(Mandatory = $true)][string]$VsTools,
    [Parameter(Mandatory = $true)][string]$WindowsSdkDir,
    [Parameter(Mandatory = $true)][string]$WindowsSdkVersion
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $LinkTool)) {
    throw "MSVC link.exe not found: $LinkTool"
}
$objects = @(Get-ChildItem -LiteralPath $ObjectDirectory -Filter '*.obj' -File |
    Sort-Object Name |
    ForEach-Object { $_.FullName })
if ($objects.Count -eq 0) {
    throw "No host objects were produced in $ObjectDirectory"
}

$sdkLib = Join-Path $WindowsSdkDir ("Lib\{0}" -f $WindowsSdkVersion)
$args = @(
    '/NOLOGO',
    '/WINMD:ONLY',
    "/WINMDFILE:$Output",
    '/MACHINE:X64',
    '/SUBSYSTEM:WINDOWS',
    "/LIBPATH:$VsTools\lib\x64\store",
    "/LIBPATH:$VsTools\lib\x64",
    "/LIBPATH:$sdkLib\ucrt\x64",
    "/LIBPATH:$sdkLib\um\x64",
    'vccorlib.lib',
    'runtimeobject.lib',
    'WindowsApp.lib',
    'ucrt.lib',
    'vcruntime.lib',
    'msvcrt.lib',
    'oldnames.lib'
) + $objects

& $LinkTool @args
if ($LASTEXITCODE -ne 0) {
    throw "MSVC WinMD generation failed with exit code $LASTEXITCODE"
}
if (-not (Test-Path -LiteralPath $Output)) {
    throw "MSVC link.exe did not produce $Output"
}
