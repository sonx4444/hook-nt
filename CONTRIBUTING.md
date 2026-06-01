# Contributing

HookNt is intentionally small. Changes should preserve a reproducible Windows x64 demo and fail closed when an instruction or process state is unsupported.

## Development Check

Run this before opening a pull request:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
powershell -ExecutionPolicy Bypass -File .\scripts\smoke.ps1 -SkipBuild
```

## Pull Requests

- Explain the supported Windows versions used for testing.
- Add or update tests for changed behavior.
- Keep buffer logging bounded.
- Add each hook as one standalone file under `src/ntdlln/hooks/` using `DEFINE_NT_HOOK` and `CALL_ORIGINAL`.
- Verify new hook exports appear in `hooknt.exe --list-hooks`.
- Document new hook APIs and their parameter handling.
- Do not weaken trampoline validation without adding relocation support and tests.
