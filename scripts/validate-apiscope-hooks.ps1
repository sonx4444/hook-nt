param(
    [Parameter(Mandatory = $true)]
    [string]$DllPath
)

$ErrorActionPreference = "Stop"

$dumpbin = (Get-Command dumpbin.exe -ErrorAction SilentlyContinue).Source
if (-not $dumpbin) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $dumpbin = & $vswhere `
            -latest `
            -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -find "VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe" |
            Select-Object -Last 1
    }
}
if (-not $dumpbin) {
    throw "Could not locate dumpbin.exe"
}

$headers = & $dumpbin /headers $DllPath
if ($LASTEXITCODE -ne 0) {
    throw "dumpbin /headers failed for $DllPath"
}

$imports = & $dumpbin /imports $DllPath
if ($LASTEXITCODE -ne 0) {
    throw "dumpbin /imports failed for $DllPath"
}

if ($imports -match "Section contains the following imports:") {
    throw "apiscope-hooks.dll must not contain imports"
}

$entryPoint = $headers | Where-Object { $_ -match "entry point" } | Select-Object -First 1
if (-not $entryPoint -or $entryPoint -notmatch "^\s*0+\s+entry point") {
    throw "apiscope-hooks.dll must have a zero entry point"
}

$tlsDirectory = $headers | Where-Object { $_ -match "Thread Storage Directory" } | Select-Object -First 1
if ($tlsDirectory -and $tlsDirectory -notmatch "^\s*0+\s+\[\s*0+\]") {
    throw "apiscope-hooks.dll must not contain TLS initialization"
}

Write-Host "Validated import-free apiscope-hooks.dll"
