param(
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

if (-not $SkipBuild) {
    cmake -S $repoRoot -B "$repoRoot\build" -A x64
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    cmake --build "$repoRoot\build" --config Release
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$outputDir = "$repoRoot\build\bin\Release"
$testFile = "$outputDir\test_file.txt"
$outputFile = "$outputDir\smoke-output.txt"
Remove-Item -LiteralPath $testFile -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $outputFile -ErrorAction SilentlyContinue

Push-Location $outputDir
try {
    $availableHooks = & .\hooknt.exe --list-hooks
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    cmd /d /s /c '"hooknt.exe" ".\test_file_ops.exe" NtCreateFile NtWriteFile NtReadFile > "smoke-output.txt" 2>&1'
    $exitCode = $LASTEXITCODE
} finally {
    Pop-Location
}

foreach ($hook in "NtCreateFile", "NtWriteFile", "NtReadFile") {
    if (-not ($availableHooks -match "^$hook$")) {
        throw "Expected discovered hook $hook was not listed"
    }
}

$output = Get-Content -LiteralPath $outputFile
$output | ForEach-Object { Write-Host $_ }

if ($exitCode -ne 0) {
    throw "hooknt.exe failed with exit code $exitCode"
}

foreach ($hook in "NtCreateFile", "NtWriteFile", "NtReadFile") {
    if (-not ($output -match "\[\*\] $hook")) {
        throw "Expected runtime log for $hook was not found"
    }
}

if (-not (Test-Path -LiteralPath $testFile)) {
    throw "Expected test file was not created"
}

if ((Get-Content -LiteralPath $testFile -Raw) -ne "Hello, HookNt!") {
    throw "Test file contents did not match"
}

Write-Host "HookNt smoke test passed."
