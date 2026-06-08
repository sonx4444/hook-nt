# Contributing

ApiScope is intentionally focused. Changes should preserve a reproducible Windows x64 tracer and fail closed when an instruction, export, or process state is unsupported.

## Development Check

Run this before opening a pull request:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
powershell -ExecutionPolicy Bypass -File .\scripts\validate-apiscope-hooks.ps1 .\build\bin\Release\apiscope-hooks.dll
powershell -ExecutionPolicy Bypass -File .\scripts\smoke.ps1 -SkipBuild
```

## Pull Requests

- Explain the supported Windows versions used for testing.
- Add or update tests for changed behavior.
- Run the smoke suite after changing launch, attach, transport, or hook installation behavior.
- Keep debug-event setup transactional: install while the target is stopped, roll back partial installation on failure, and always continue or detach the target.
- Keep buffer logging bounded.
- Keep `apiscope-hooks.dll` import-free, without an entry point or TLS callbacks; the mapper intentionally does not initialize a general-purpose DLL.
- Send hook data through `trace_transport.h`; do not call imported logging or file APIs from injected hook code.
- Declare hook fields locally with the generic trace builder and lower `snake_case` names; do not add API-specific launcher rendering.
- Add each hook as one standalone file under `src/apiscope-hooks/hooks/` using `DEFINE_API_HOOK` and `CALL_ORIGINAL`.
- Use a lowercase normalized DLL name and the exact case-sensitive export name.
- Verify new descriptors appear in `apiscope.exe --list-hooks`.
- Document new hook APIs and their parameter handling.
- Do not weaken trampoline validation without adding relocation support and tests.
