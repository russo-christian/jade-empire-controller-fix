param(
    [string]$OutputDirectory = 'build'
)
$ErrorActionPreference = 'Stop'
$jadeRoot = Split-Path -Parent $PSScriptRoot
$jadeOutput = if ([IO.Path]::IsPathRooted($OutputDirectory)) {
    [IO.Path]::GetFullPath($OutputDirectory)
} else {
    [IO.Path]::GetFullPath((Join-Path $jadeRoot $OutputDirectory))
}
$jadeVswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $jadeVswhere)) { throw 'Install Visual Studio C++ Build Tools and a Windows SDK.' }
$jadeVs = & $jadeVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $jadeVs) { throw 'Visual Studio C++ Build Tools are required.' }
$jadeVc = (Get-ChildItem -LiteralPath "$jadeVs\VC\Tools\MSVC" -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1).FullName
$jadeSdk = "${env:ProgramFiles(x86)}\Windows Kits\10"
$jadeSdkVersion = (Get-ChildItem -LiteralPath "$jadeSdk\Include" -Directory | Where-Object { Test-Path -LiteralPath "$($_.FullName)\um\Windows.h" } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1).Name
if (-not $jadeSdkVersion) { throw 'Windows SDK headers are required.' }
$jadeCompiler = "$jadeVc\bin\Hostx64\x86\cl.exe"
$jadeCompileFlags = @('/nologo', '/W4', '/WX', '/EHsc', '/MT', '/O2', '/std:c++17',
    "/I$jadeVc\include", "/I$jadeSdk\Include\$jadeSdkVersion\shared",
    "/I$jadeSdk\Include\$jadeSdkVersion\um", "/I$jadeSdk\Include\$jadeSdkVersion\ucrt")
$jadeLinkFlags = @('/link', "/LIBPATH:$jadeVc\lib\x86", "/LIBPATH:$jadeSdk\Lib\$jadeSdkVersion\um\x86",
    "/LIBPATH:$jadeSdk\Lib\$jadeSdkVersion\ucrt\x86", 'kernel32.lib', 'user32.lib')
$jadeObjectDirectory = Join-Path $jadeOutput 'obj'
$jadeHeaderDirectory = Join-Path $jadeOutput 'header-checks'
New-Item -ItemType Directory -Force -Path $jadeOutput, $jadeObjectDirectory, $jadeHeaderDirectory | Out-Null

# Each header must supply its own dependencies and tolerate repeated inclusion.
foreach ($jadeHeader in Get-ChildItem -LiteralPath (Join-Path $jadeRoot 'src') -Filter '*.hpp') {
    $jadeCheck = Join-Path $jadeHeaderDirectory ($jadeHeader.BaseName + '.cpp')
    $jadeInclude = '#include "' + $jadeHeader.Name + '"'
    [IO.File]::WriteAllText($jadeCheck, "$jadeInclude`n$jadeInclude`n")
    & $jadeCompiler @jadeCompileFlags /Zs "/I$jadeRoot\src" $jadeCheck
    if ($LASTEXITCODE -ne 0) { throw "Header is not self-contained: $($jadeHeader.Name)" }
}

# Compile each implementation independently; no implementation-header include order.
$jadeObjects = @()
foreach ($jadeSource in Get-ChildItem -LiteralPath (Join-Path $jadeRoot 'src') -Filter '*.cpp') {
    $jadeObject = Join-Path $jadeObjectDirectory ($jadeSource.BaseName + '.obj')
    & $jadeCompiler @jadeCompileFlags /c $jadeSource.FullName "/Fo:$jadeObject"
    if ($LASTEXITCODE -ne 0) { throw "Compile failed: $($jadeSource.Name)" }
    $jadeObjects += $jadeObject
}
# Export ordinals deliberately match Windows dinput8; suppress only that advisory.
# The SDK library supplies the fallback joystick format as static data.
& $jadeCompiler /nologo /LD @jadeObjects "/Fe:$jadeOutput\dinput8.dll" @jadeLinkFlags 'dxguid.lib' "$jadeSdk\Lib\$jadeSdkVersion\um\x86\dinput8.lib" "/DEF:$jadeRoot\src\dinput8.def" /IGNORE:4222
if ($LASTEXITCODE -ne 0) { throw 'DirectInput proxy link failed.' }

function Build-Test([string]$Name, [string]$Source, [string[]]$Objects = @(), [string[]]$LinkOptions = @()) {
    & $jadeCompiler @jadeCompileFlags (Join-Path $jadeRoot "tests\$Source") @Objects "/Fo:$jadeObjectDirectory\test_$Name.obj" "/Fe:$jadeOutput\test_$Name.exe" @jadeLinkFlags @LinkOptions
    if ($LASTEXITCODE -ne 0) { throw "Test build failed: $Name" }
}
Build-Test 'control_math' 'control_math.cpp' @((Join-Path $jadeObjectDirectory 'control_math.obj'))
& (Join-Path $jadeOutput 'test_control_math.exe')
if ($LASTEXITCODE -ne 0) { throw 'Direction checks failed.' }
Build-Test 'controller_bindings' 'controller_bindings.cpp' @((Join-Path $jadeObjectDirectory 'controller_bindings.obj'))
& (Join-Path $jadeOutput 'test_controller_bindings.exe')
if ($LASTEXITCODE -ne 0) { throw 'Controller binding setup checks failed.' }
Build-Test 'glyph_assets' 'glyph_assets.cpp' @((Join-Path $jadeObjectDirectory 'glyph_assets.obj'))
& (Join-Path $jadeOutput 'test_glyph_assets.exe')
if ($LASTEXITCODE -ne 0) { throw 'Glyph decoder checks failed.' }
Build-Test 'proxy_forwarding' 'proxy_forwarding.cpp' -LinkOptions @('dxguid.lib', "$jadeSdk\Lib\$jadeSdkVersion\um\x86\dinput8.lib")
& (Join-Path $jadeOutput 'test_proxy_forwarding.exe') (Join-Path $jadeOutput 'dinput8.dll')
if ($LASTEXITCODE -ne 0) { throw 'Proxy forwarding checks failed.' }
& (Join-Path $jadeOutput 'test_proxy_forwarding.exe') (Join-Path $jadeOutput 'dinput8.dll') --without-joystick-export
if ($LASTEXITCODE -ne 0) { throw 'Proxy missing-export checks failed.' }

# A rebuild must not overwrite locally chosen options.
$jadeIni = Join-Path $jadeOutput 'JadeEmpireControllerFix.ini'
if (-not (Test-Path -LiteralPath $jadeIni)) {
    Copy-Item -LiteralPath (Join-Path $jadeRoot 'JadeEmpireControllerFix.ini') -Destination $jadeIni
}
Write-Output "Built dinput8.dll and completed the checks in $jadeOutput"
