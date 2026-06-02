# Contributing

HookNt is intentionally small. Changes should preserve a reproducible Windows x64 demo and fail closed when an instruction or process state is unsupported.

## Development Check

Run this before opening a pull request:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
powershell -ExecutionPolicy Bypass -File .\scripts\validate-ntdlln.ps1 .\build\bin\Release\ntdlln.dll
powershell -ExecutionPolicy Bypass -File .\scripts\smoke.ps1 -SkipBuild
```

## Pull Requests

- Explain the supported Windows versions used for testing.
- Add or update tests for changed behavior.
- Run the smoke suite after changing launch, attach, transport, or hook installation behavior.
- Keep attach setup transactional: suspend before patching, roll back partial hook installation on failure, and resume the target whenever recovery is possible.
- Keep buffer logging bounded.
- Keep `ntdlln.dll` import-free, without an entry point or TLS callbacks; the mapper intentionally does not initialize a general-purpose DLL.
- Send hook data through `trace_transport.h`; do not call imported logging or file APIs from injected hook code.
- Declare hook fields locally with the generic trace builder and lower `snake_case` names; do not add API-specific launcher rendering.
- Add each hook as one standalone file under `src/ntdlln/hooks/` using `DEFINE_NT_HOOK` and `CALL_ORIGINAL`.
- Verify new hook exports appear in `hooknt.exe --list-hooks`.
- Document new hook APIs and their parameter handling.
- Do not weaken trampoline validation without adding relocation support and tests.
