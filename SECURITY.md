# Security Policy

ApiScope modifies process memory and is intended for authorized educational and research use on Windows systems you control.

Launching or attaching under the Windows debugger temporarily stops target threads and writes executable code into the process. Do not instrument production workloads, security-critical processes, or processes you are not authorized to inspect. Elevated targets may require running ApiScope with elevated rights.

ApiScope disables the debugger's kill-on-exit behavior. Forced controller
termination therefore leaves the target alive, but cannot restore hooks or
release the manually mapped hook image. Prefer Ctrl+C for a clean detach.

## Reporting A Vulnerability

Do not include sensitive exploit details in a public issue. Report vulnerabilities through GitHub private vulnerability reporting when available, or contact the repository owner privately.

Include the affected commit, Windows version, reproduction steps, and expected impact.

## Supported Versions

Security fixes are applied to the latest commit on `main`. There is no production support guarantee for pre-release versions.
