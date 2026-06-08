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
$textOutput = "$outputDir\smoke-output.txt"
$createOutput = "$outputDir\smoke-create-output.txt"
$jsonOutput = "$outputDir\smoke-output.jsonl"
$jsonTargetOutput = "$outputDir\smoke-json-target-output.txt"
$quietJsonOutput = "$outputDir\smoke-quiet-output.jsonl"
$quietTargetOutput = "$outputDir\smoke-quiet-target-output.txt"
$multithreadJsonOutput = "$outputDir\smoke-multithread-output.jsonl"
$multithreadTargetOutput = "$outputDir\smoke-multithread-target-output.txt"
$attachJsonOutput = "$outputDir\smoke-attach-output.jsonl"
$attachTargetOutput = "$outputDir\smoke-attach-target-output.txt"
$bcryptJsonOutput = "$outputDir\smoke-bcrypt-output.jsonl"
$bcryptTargetOutput = "$outputDir\smoke-bcrypt-target-output.txt"
$unicodeSuffix = "$([char]0x6E2C)$([char]0x8A66)"
$unicodeDir = "$outputDir\unicode target $unicodeSuffix"
$unicodeTarget = "$unicodeDir\test-file-ops-unicode.exe"
$relativeUnicodeTarget = ".\unicode target $unicodeSuffix\test-file-ops-unicode.exe"

Remove-Item -LiteralPath $testFile, $textOutput, $createOutput, $jsonOutput, $jsonTargetOutput, $quietJsonOutput, $quietTargetOutput, $multithreadJsonOutput, $multithreadTargetOutput, $attachJsonOutput, $attachTargetOutput, $bcryptJsonOutput, $bcryptTargetOutput -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $unicodeDir -Force | Out-Null
Copy-Item -LiteralPath "$outputDir\test_file_ops.exe" -Destination $unicodeTarget -Force

Push-Location $outputDir
try {
    $availableHooks = & .\apiscope.exe --list-hooks
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $longVersion = & .\apiscope.exe --version
    if ($LASTEXITCODE -ne 0) { throw "--version failed" }
    $shortVersion = & .\apiscope.exe -V
    if ($LASTEXITCODE -ne 0 -or $shortVersion -ne $longVersion -or $longVersion -notmatch "^apiscope \d+\.\d+\.\d+$") {
        throw "Version output did not match"
    }

    cmd /d /s /c '"apiscope.exe" run -k all -- ".\test_file_ops.exe" > "smoke-output.txt" 2>&1'
    if ($LASTEXITCODE -ne 0) { throw "Text smoke trace failed" }

    cmd /d /s /c '"apiscope.exe" run --hook ntdll.dll!NtCreateFile -- ".\test_file_ops.exe" > "smoke-create-output.txt" 2>&1'
    if ($LASTEXITCODE -ne 0) { throw "NtCreateFile-only smoke trace failed" }

    $jsonCommand = '"apiscope.exe" run --hook all --format jsonl --output "smoke-output.jsonl" -- "' +
        $relativeUnicodeTarget + '" "argument with spaces" > "smoke-json-target-output.txt" 2>&1'
    cmd /d /s /c $jsonCommand
    if ($LASTEXITCODE -ne 0) { throw "JSONL smoke trace failed" }

    cmd /d /s /c '"apiscope.exe" run --hook all --quiet --format jsonl --output "smoke-quiet-output.jsonl" -- ".\test_file_ops.exe" > "smoke-quiet-target-output.txt" 2>&1'
    if ($LASTEXITCODE -ne 0) { throw "Quiet JSONL smoke trace failed" }

    cmd /d /s /c '"apiscope.exe" run --hook all --quiet --format jsonl --output "smoke-multithread-output.jsonl" -- ".\test_multithread_file_ops.exe" --iterations 128 > "smoke-multithread-target-output.txt" 2>&1'
    if ($LASTEXITCODE -ne 0) { throw "Multithreaded JSONL smoke trace failed" }

    $attachTarget = Start-Process -FilePath ".\test_multithread_file_ops.exe" -ArgumentList "--delay-ms", "3000", "--iterations", "128", "--linger-ms", "1000" -PassThru -WindowStyle Hidden
    try {
        $attachCommand = '"apiscope.exe" attach -p ' + $attachTarget.Id +
            ' -k all -q -f jsonl -o "smoke-attach-output.jsonl" > "smoke-attach-target-output.txt" 2>&1'
        cmd /d /s /c $attachCommand
        if ($LASTEXITCODE -ne 0) { throw "Attach JSONL smoke trace failed" }
    } finally {
        if (-not $attachTarget.HasExited) {
            Stop-Process -Id $attachTarget.Id -Force
        }
    }

    cmd /d /s /c '"apiscope.exe" run -k bcrypt.dll!BCryptOpenAlgorithmProvider -q -f jsonl -o "smoke-bcrypt-output.jsonl" -- ".\test_delayed_bcrypt.exe" > "smoke-bcrypt-target-output.txt" 2>&1'
    if ($LASTEXITCODE -ne 0) { throw "Delayed bcrypt trace failed" }

    $detachTarget = Start-Process -FilePath ".\test_multithread_file_ops.exe" -ArgumentList "--delay-ms", "3000", "--iterations", "2" -PassThru -WindowStyle Hidden
    try {
        & .\test_send_ctrl_break.exe 1000 .\apiscope.exe attach -p $detachTarget.Id -k ntdll.dll!NtCreateFile
        if ($LASTEXITCODE -ne 0) { throw "Ctrl+Break detach failed" }
        $detachTarget.Refresh()
        if ($detachTarget.HasExited) { throw "Ctrl+Break detach terminated the target" }
        if (-not $detachTarget.WaitForExit(10000) -or $detachTarget.ExitCode -ne 0) {
            throw "Target did not continue successfully after detach"
        }
    } finally {
        if (-not $detachTarget.HasExited) {
            Stop-Process -Id $detachTarget.Id -Force
        }
    }

    $survivalTarget = Start-Process -FilePath ".\test_multithread_file_ops.exe" -ArgumentList "--delay-ms", "3000", "--iterations", "2" -PassThru -WindowStyle Hidden
    $controller = $null
    try {
        $controller = Start-Process -FilePath ".\apiscope.exe" -ArgumentList "attach", "-p", $survivalTarget.Id, "-k", "ntdll.dll!NtCreateFile" -PassThru -WindowStyle Hidden
        Start-Sleep -Milliseconds 1000
        Stop-Process -Id $controller.Id -Force
        $controller.WaitForExit()
        $survivalTarget.Refresh()
        if ($survivalTarget.HasExited) { throw "Unexpected controller exit terminated the target" }
        if (-not $survivalTarget.WaitForExit(10000) -or $survivalTarget.ExitCode -ne 0) {
            throw "Target did not survive unexpected controller exit"
        }
    } finally {
        if ($controller -and -not $controller.HasExited) {
            Stop-Process -Id $controller.Id -Force
        }
        if (-not $survivalTarget.HasExited) {
            Stop-Process -Id $survivalTarget.Id -Force
        }
    }
} finally {
    Pop-Location
}

foreach ($hook in "ntdll.dll!NtCreateFile", "ntdll.dll!NtWriteFile", "ntdll.dll!NtReadFile", "bcrypt.dll!BCryptOpenAlgorithmProvider") {
    if (-not ($availableHooks -match "^$hook$")) {
        throw "Expected discovered hook $hook was not listed"
    }
}

$output = Get-Content -LiteralPath $textOutput
$output | ForEach-Object { Write-Host $_ }
foreach ($hook in "NtCreateFile", "NtWriteFile", "NtReadFile") {
    if (-not ($output -match "\[\*\] ntdll\.dll!$hook")) {
        throw "Expected runtime log for $hook was not found"
    }
}
if (-not ($output -match "Target process exited with status 0 \(0x00000000\)")) {
    throw "Target exit status was not reported"
}
if (($output -match "\[\*\] ntdll\.dll!NtWriteFile").Count -ge 10) {
    throw "NtWriteFile logging appears recursive"
}

$createOnly = Get-Content -LiteralPath $createOutput
if (-not ($createOnly -match "\[\*\] ntdll\.dll!NtCreateFile")) {
    throw "NtCreateFile-only trace did not produce events"
}

$jsonEvents = Get-Content -LiteralPath $jsonOutput | ForEach-Object { $_ | ConvertFrom-Json }
foreach ($hook in "NtCreateFile", "NtWriteFile", "NtReadFile") {
    if (-not ($jsonEvents.api -contains $hook)) {
        throw "JSONL output did not contain $hook"
    }
}
$jsonWriteEvents = @($jsonEvents | Where-Object { $_.api -eq "NtWriteFile" -and $_.fields.buffer })
if ($jsonWriteEvents.Count -eq 0 -or $jsonWriteEvents[0].fields.buffer.type -ne "bytes") {
    throw "JSONL output did not contain a generic buffer field"
}
if (-not $jsonEvents[0].timestamp -or
    -not $jsonEvents[0].timestamp_100ns -or
    -not $jsonEvents[0].thread_id) {
    throw "JSONL output did not contain timestamp and thread metadata"
}

$jsonTarget = Get-Content -LiteralPath $jsonTargetOutput
if (-not ($jsonTarget -match "\[\*\] ntdll\.dll!NtWriteFile")) {
    throw "JSONL output was not mirrored as terminal text"
}

$quietTarget = Get-Content -LiteralPath $quietTargetOutput
if ($quietTarget -match "\[\*\]") {
    throw "Quiet mode did not suppress the terminal mirror"
}
$quietJsonEvents = Get-Content -LiteralPath $quietJsonOutput | ForEach-Object { $_ | ConvertFrom-Json }
if (-not ($quietJsonEvents.api -contains "NtWriteFile")) {
    throw "Quiet JSONL output did not contain NtWriteFile"
}

$multithreadJsonEvents = Get-Content -LiteralPath $multithreadJsonOutput | ForEach-Object { $_ | ConvertFrom-Json }
$multithreadFileEvents = @($multithreadJsonEvents | Where-Object {
    $_.api -eq "NtCreateFile" -or $_.api -eq "NtWriteFile" -or $_.api -eq "NtReadFile"
})
$multithreadIds = @($multithreadFileEvents.thread_id | Sort-Object -Unique)
if ($multithreadIds.Count -lt 2) {
    throw "Multithreaded JSONL output did not contain multiple worker thread IDs"
}
$multithreadTarget = Get-Content -LiteralPath $multithreadTargetOutput
if ($multithreadTarget -match "Trace events dropped") {
    throw "Queued multithreaded trace reported dropped events"
}
if ($multithreadFileEvents.Count -lt (8 * 128 * 3)) {
    throw "Queued multithreaded trace did not capture all expected file operations"
}
if (-not ($multithreadFileEvents.api -contains "NtCreateFile") -or
    -not ($multithreadFileEvents.api -contains "NtWriteFile") -or
    -not ($multithreadFileEvents.api -contains "NtReadFile")) {
    throw "Multithreaded JSONL output did not contain all expected file APIs"
}

$attachJsonEvents = Get-Content -LiteralPath $attachJsonOutput | ForEach-Object { $_ | ConvertFrom-Json }
$attachFileEvents = @($attachJsonEvents | Where-Object {
    $_.api -eq "NtCreateFile" -or $_.api -eq "NtWriteFile" -or $_.api -eq "NtReadFile"
})
$attachIds = @($attachFileEvents.thread_id | Sort-Object -Unique)
if ($attachIds.Count -lt 2) {
    throw "Attach JSONL output did not contain multiple worker thread IDs"
}
if (-not ($attachFileEvents.api -contains "NtCreateFile") -or
    -not ($attachFileEvents.api -contains "NtWriteFile") -or
    -not ($attachFileEvents.api -contains "NtReadFile")) {
    throw "Attach JSONL output did not contain all expected file APIs"
}

if (-not (Test-Path -LiteralPath $testFile)) {
    throw "Expected test file was not created"
}
if ((Get-Content -LiteralPath $testFile -Raw) -ne "Hello, ApiScope!") {
    throw "Test file contents did not match"
}

$bcryptEvents = Get-Content -LiteralPath $bcryptJsonOutput | ForEach-Object { $_ | ConvertFrom-Json }
if (-not ($bcryptEvents.hook -contains "bcrypt.dll!BCryptOpenAlgorithmProvider")) {
    throw "Delayed bcrypt module hook did not produce an event"
}

Write-Host "ApiScope smoke test passed."
