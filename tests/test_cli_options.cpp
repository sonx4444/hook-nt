#include "cli_options.h"
#include <stdio.h>

static CommandLineOptions Parse(
    std::vector<std::wstring> arguments,
    const std::vector<HookDefinition>& availableHooks) {
    std::vector<wchar_t*> pointers;
    for (std::wstring& argument : arguments) {
        pointers.push_back(argument.data());
    }
    return ParseCommandLine((int)pointers.size(), pointers.data(), availableHooks);
}

int main() {
    std::vector<HookDefinition> availableHooks = {
        {"bcrypt.dll", "BCryptOpenAlgorithmProvider", "", "", "bcrypt.dll!BCryptOpenAlgorithmProvider"},
        {"ntdll.dll", "NtCreateFile", "", "", "ntdll.dll!NtCreateFile"},
        {"ntdll.dll", "NtReadFile", "", "", "ntdll.dll!NtReadFile"},
        {"ntdll.dll", "NtWriteFile", "", "", "ntdll.dll!NtWriteFile"}};

    CommandLineOptions structured = Parse(
        {L"apiscope.exe",
         L"run",
         L"--hook",
         L"all",
         L"--format",
         L"jsonl",
         L"--output",
         L"trace.jsonl",
         L"--quiet",
         L"--",
         L"C:\\Program Files\\test.exe",
         L"argument with spaces"},
        availableHooks);
    if (structured.mode != CommandMode::Run ||
        structured.hooks.size() != availableHooks.size() ||
        structured.targetArguments.size() != 2 ||
        structured.output.format != TraceOutputFormat::Jsonl ||
        !structured.output.quiet) {
        printf("Structured command did not parse\n");
        return 1;
    }

    CommandLineOptions shortOptions = Parse(
        {L"apiscope.exe",
         L"run",
         L"-k",
         L"ntdll.dll!NtCreateFile",
         L"-f",
         L"jsonl",
         L"-o",
         L"trace.jsonl",
         L"-q",
         L"--",
         L"test.exe"},
        availableHooks);
    if (shortOptions.mode != CommandMode::Run ||
        shortOptions.hooks.size() != 1 ||
        shortOptions.hooks[0].canonicalName != "ntdll.dll!NtCreateFile" ||
        shortOptions.output.format != TraceOutputFormat::Jsonl ||
        shortOptions.output.outputPath != L"trace.jsonl" ||
        !shortOptions.output.quiet) {
        printf("Short run options did not parse\n");
        return 1;
    }

    if (Parse({L"apiscope.exe", L"-l"}, availableHooks).mode != CommandMode::ListHooks ||
        Parse({L"apiscope.exe", L"-h"}, availableHooks).mode != CommandMode::Help ||
        Parse({L"apiscope.exe", L"--version"}, availableHooks).mode != CommandMode::Version ||
        Parse({L"apiscope.exe", L"-V"}, availableHooks).mode != CommandMode::Version) {
        printf("Short top-level options did not parse\n");
        return 1;
    }

    CommandLineOptions attach = Parse(
        {L"apiscope.exe",
         L"attach",
         L"-p",
         L"4242",
         L"-k",
         L"all",
         L"-q",
         L"-f",
         L"jsonl",
         L"-o",
         L"attach.jsonl"},
        availableHooks);
    if (attach.mode != CommandMode::Attach ||
        attach.targetProcessId != 4242 ||
        attach.hooks.size() != availableHooks.size() ||
        !attach.output.quiet ||
        attach.output.format != TraceOutputFormat::Jsonl ||
        attach.output.outputPath != L"attach.jsonl") {
        printf("Attach command did not parse\n");
        return 1;
    }

    CommandLineOptions invalidAttachPid = Parse(
        {L"apiscope.exe", L"attach", L"-p", L"invalid", L"-k", L"all"},
        availableHooks);
    if (invalidAttachPid.mode != CommandMode::Error) {
        printf("Invalid attach process ID was not rejected\n");
        return 1;
    }

    std::wstring commandLine = BuildWindowsCommandLine(structured.targetArguments);
    if (commandLine != L"\"C:\\Program Files\\test.exe\" \"argument with spaces\"") {
        printf("Windows command line quoting did not match\n");
        return 1;
    }

    CommandLineOptions legacy = Parse(
        {L"apiscope.exe", L"test.exe", L"ntdll.dll!NtCreateFile"},
        availableHooks);
    if (legacy.mode != CommandMode::Error) {
        printf("Legacy command was not rejected\n");
        return 1;
    }

    CommandLineOptions terminalOnly = Parse(
        {L"apiscope.exe", L"run", L"--hook", L"ntdll.dll!NtCreateFile", L"--", L"test.exe"},
        availableHooks);
    if (terminalOnly.mode != CommandMode::Run ||
        terminalOnly.output.quiet ||
        !terminalOnly.output.outputPath.empty()) {
        printf("Terminal-only command did not parse\n");
        return 1;
    }

    CommandLineOptions invalidFormat = Parse(
        {L"apiscope.exe", L"run", L"--hook", L"all", L"--format", L"jsonl", L"--", L"test.exe"},
        availableHooks);
    if (invalidFormat.mode != CommandMode::Error) {
        printf("Format without output path was not rejected\n");
        return 1;
    }

    CommandLineOptions invalidQuiet = Parse(
        {L"apiscope.exe", L"run", L"--hook", L"all", L"--quiet", L"--", L"test.exe"},
        availableHooks);
    if (invalidQuiet.mode != CommandMode::Error) {
        printf("Quiet mode without output path was not rejected\n");
        return 1;
    }

    CommandLineOptions invalidRunPid = Parse(
        {L"apiscope.exe", L"run", L"-p", L"4242", L"-k", L"all", L"--", L"test.exe"},
        availableHooks);
    if (invalidRunPid.mode != CommandMode::Error) {
        printf("Run process ID was not rejected\n");
        return 1;
    }

    CommandLineOptions unqualified = Parse(
        {L"apiscope.exe", L"run", L"-k", L"NtCreateFile", L"--", L"test.exe"},
        availableHooks);
    if (unqualified.mode != CommandMode::Error) {
        printf("Unqualified hook name was not rejected\n");
        return 1;
    }

    CommandLineOptions moduleCase = Parse(
        {L"apiscope.exe", L"run", L"-k", L"NTDLL.DLL!NtCreateFile", L"--", L"test.exe"},
        availableHooks);
    if (moduleCase.mode != CommandMode::Run ||
        moduleCase.hooks[0].canonicalName != "ntdll.dll!NtCreateFile") {
        printf("Case-insensitive module name was not accepted\n");
        return 1;
    }

    CommandLineOptions exportCase = Parse(
        {L"apiscope.exe", L"run", L"-k", L"ntdll.dll!ntcreatefile", L"--", L"test.exe"},
        availableHooks);
    if (exportCase.mode != CommandMode::Error) {
        printf("Case-insensitive export name was accepted\n");
        return 1;
    }

    return 0;
}
