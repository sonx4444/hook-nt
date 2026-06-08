# Roadmap

## v0.1.0: Credible Demo

- Reproducible Windows x64 build
- Windows CI and runtime smoke test
- Bounded logging for `NtCreateFile`, `NtReadFile`, and `NtWriteFile`
- Conservative trampoline validation

## v0.2.0: Trace Pipeline

- Import-free injected DLL contract with build-time validation
- Nonblocking named-pipe event transport with drop counting
- Unpatched `NtWriteFile` and `NtReadVirtualMemory` transport bypass trampolines
- Self-describing binary TLV events for hook-local schemas
- Hook-entry timestamps and originating thread IDs
- Readable terminal rendering with optional text or JSONL file tee and `--quiet`
- Structured `run --hook ... -- <program> [args...]` CLI and `--hook all`
- Unicode target paths and forwarded target arguments

## v0.3.0: Attach Workflow

- Short CLI aliases and `--version`
- Attach to an existing PID with a suspended transactional setup window
- Real multithreaded launch and attach smoke coverage

## v0.4.0: ApiScope

- Hard rename to ApiScope
- Module-qualified, self-describing hook descriptors
- Unified debugger event loop for launch and attach
- Delayed DLL hook installation and unload tracking
- Clean Ctrl+C restoration and debugger detach
- Generic trace protocol v5
- `bcrypt.dll!BCryptOpenAlgorithmProvider` sample hook

## Next

- Path and API filters
- Metadata-rich hook registry for categories and default filters
- Additional NT and Win32 APIs
- Ordinal forwarder support
- Relocation of RIP-relative and relative-control-flow trampoline instructions
