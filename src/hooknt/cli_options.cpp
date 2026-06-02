#include "cli_options.h"
#include "hook_registry.h"
#include <algorithm>
#include <stdint.h>
#include <stdio.h>

static std::string NarrowHookName(const std::wstring& value) {
    std::string result;
    for (wchar_t character : value) {
        if (character < 0 || character > 0x7F) {
            return "";
        }
        result.push_back((char)character);
    }
    return result;
}

static void AddHook(std::vector<std::string>* hooks, const std::string& hook) {
    if (std::find(hooks->begin(), hooks->end(), hook) == hooks->end()) {
        hooks->push_back(hook);
    }
}

static bool ExpandAndValidateHooks(
    CommandLineOptions* options,
    const std::vector<std::string>& availableHooks,
    const std::vector<std::string>& requestedHooks) {
    for (const std::string& hook : requestedHooks) {
        if (hook == "all") {
            for (const std::string& availableHook : availableHooks) {
                AddHook(&options->hooks, availableHook);
            }
            continue;
        }
        if (!IsSupportedHook(availableHooks, hook)) {
            options->error = "Unsupported hook: " + hook;
            return false;
        }
        AddHook(&options->hooks, hook);
    }

    if (options->hooks.empty()) {
        options->error = "At least one --hook is required";
        return false;
    }
    return true;
}

static std::wstring QuoteWindowsArgument(const std::wstring& argument) {
    if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return argument;
    }

    std::wstring quoted = L"\"";
    size_t backslashes = 0;
    for (wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(L'"');
            backslashes = 0;
            continue;
        }
        quoted.append(backslashes, L'\\');
        backslashes = 0;
        quoted.push_back(character);
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

static bool ParseProcessId(const std::wstring& value, DWORD* processId) {
    if (!processId || value.empty()) {
        return false;
    }

    uint64_t parsed = 0;
    for (wchar_t character : value) {
        if (character < L'0' || character > L'9') {
            return false;
        }
        parsed = parsed * 10 + (character - L'0');
        if (parsed > UINT32_MAX) {
            return false;
        }
    }
    if (parsed == 0) {
        return false;
    }

    *processId = (DWORD)parsed;
    return true;
}

CommandLineOptions ParseCommandLine(
    int argc,
    wchar_t* argv[],
    const std::vector<std::string>& availableHooks) {
    CommandLineOptions options = {};
    options.mode = CommandMode::Error;
    options.output.format = TraceOutputFormat::Text;

    if (argc == 2 && (std::wstring(argv[1]) == L"--list-hooks" || std::wstring(argv[1]) == L"-l")) {
        options.mode = CommandMode::ListHooks;
        return options;
    }
    if (argc == 1 || (argc == 2 && (std::wstring(argv[1]) == L"--help" || std::wstring(argv[1]) == L"-h"))) {
        options.mode = CommandMode::Help;
        return options;
    }
    if (argc == 2 && (std::wstring(argv[1]) == L"--version" || std::wstring(argv[1]) == L"-V")) {
        options.mode = CommandMode::Version;
        return options;
    }

    if (argc >= 2 && (std::wstring(argv[1]) == L"run" || std::wstring(argv[1]) == L"attach")) {
        bool attach = std::wstring(argv[1]) == L"attach";
        std::vector<std::string> requestedHooks;
        bool formatSpecified = false;
        int index = 2;
        for (; index < argc; ++index) {
            std::wstring argument = argv[index];
            if (argument == L"--") {
                if (attach) {
                    options.error = "attach does not accept a target command";
                    return options;
                }
                ++index;
                break;
            }
            if (argument == L"--pid" || argument == L"-p") {
                if (!attach) {
                    options.error = "--pid is only valid with attach";
                    return options;
                }
                if (++index >= argc || !ParseProcessId(argv[index], &options.targetProcessId)) {
                    options.error = "--pid requires a positive process ID";
                    return options;
                }
                continue;
            }
            if (argument == L"--hook" || argument == L"-k") {
                if (++index >= argc) {
                    options.error = "--hook requires a value";
                    return options;
                }
                std::string hook = NarrowHookName(argv[index]);
                if (hook.empty()) {
                    options.error = "Hook names must be ASCII";
                    return options;
                }
                requestedHooks.push_back(hook);
                continue;
            }
            if (argument == L"--format" || argument == L"-f") {
                if (++index >= argc) {
                    options.error = "--format requires text or jsonl";
                    return options;
                }
                std::wstring format = argv[index];
                if (format == L"text") {
                    options.output.format = TraceOutputFormat::Text;
                } else if (format == L"jsonl") {
                    options.output.format = TraceOutputFormat::Jsonl;
                } else {
                    options.error = "--format requires text or jsonl";
                    return options;
                }
                formatSpecified = true;
                continue;
            }
            if (argument == L"--output" || argument == L"-o") {
                if (++index >= argc) {
                    options.error = "--output requires a path";
                    return options;
                }
                options.output.outputPath = argv[index];
                continue;
            }
            if (argument == L"--quiet" || argument == L"-q") {
                options.output.quiet = true;
                continue;
            }
            options.error = "Unknown command option";
            return options;
        }

        if (attach) {
            if (index != argc) {
                options.error = "attach accepts options only";
                return options;
            }
            if (options.targetProcessId == 0) {
                options.error = "attach requires --pid <pid>";
                return options;
            }
        } else {
            if (index >= argc) {
                options.error = "run requires -- followed by a target program";
                return options;
            }
            for (; index < argc; ++index) {
                options.targetArguments.push_back(argv[index]);
            }
        }
        if (formatSpecified && options.output.outputPath.empty()) {
            options.error = "--format requires --output <path>";
            return options;
        }
        if (options.output.quiet && options.output.outputPath.empty()) {
            options.error = "--quiet requires --output <path>";
            return options;
        }
        if (!ExpandAndValidateHooks(&options, availableHooks, requestedHooks)) {
            return options;
        }
        options.mode = attach ? CommandMode::Attach : CommandMode::Run;
        return options;
    }

    options.error = "Expected 'run', 'attach', --list-hooks, --help, or --version";
    return options;
}

std::wstring BuildWindowsCommandLine(const std::vector<std::wstring>& arguments) {
    std::wstring commandLine;
    for (const std::wstring& argument : arguments) {
        if (!commandLine.empty()) {
            commandLine.push_back(L' ');
        }
        commandLine += QuoteWindowsArgument(argument);
    }
    return commandLine;
}

void PrintUsage() {
    printf("Usage:\n");
    printf("  hooknt.exe [--help | --version | --list-hooks]\n");
    printf("  hooknt.exe run -k <name|all> [-k <name>] [-f text|jsonl] [-o <path>] [-q] -- <program> [args...]\n");
    printf("  hooknt.exe attach -p <pid> -k <name|all> [-k <name>] [-f text|jsonl] [-o <path>] [-q]\n");
    printf("\nOptions:\n");
    printf("  -h, --help              Show this help\n");
    printf("  -V, --version           Show version\n");
    printf("  -l, --list-hooks        List supported hooks\n");
    printf("  -p, --pid <pid>         Attach to a running process ID\n");
    printf("  -k, --hook <name|all>   Enable a hook\n");
    printf("  -f, --format <format>   File output format: text or jsonl\n");
    printf("  -o, --output <path>     Tee events to a file\n");
    printf("  -q, --quiet             Suppress terminal event rendering\n");
}
