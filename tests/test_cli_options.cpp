#include "cli_options.h"
#include <stdio.h>

static CommandLineOptions Parse(
    std::vector<std::wstring> arguments,
    const std::vector<std::string>& availableHooks) {
    std::vector<wchar_t*> pointers;
    for (std::wstring& argument : arguments) {
        pointers.push_back(argument.data());
    }
    return ParseCommandLine((int)pointers.size(), pointers.data(), availableHooks);
}

int main() {
    std::vector<std::string> availableHooks = {"NtCreateFile", "NtReadFile", "NtWriteFile"};

    CommandLineOptions structured = Parse(
        {L"hooknt.exe",
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
        {L"hooknt.exe",
         L"run",
         L"-k",
         L"NtCreateFile",
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
        shortOptions.hooks[0] != "NtCreateFile" ||
        shortOptions.output.format != TraceOutputFormat::Jsonl ||
        shortOptions.output.outputPath != L"trace.jsonl" ||
        !shortOptions.output.quiet) {
        printf("Short run options did not parse\n");
        return 1;
    }

    if (Parse({L"hooknt.exe", L"-l"}, availableHooks).mode != CommandMode::ListHooks ||
        Parse({L"hooknt.exe", L"-h"}, availableHooks).mode != CommandMode::Help ||
        Parse({L"hooknt.exe", L"--version"}, availableHooks).mode != CommandMode::Version ||
        Parse({L"hooknt.exe", L"-V"}, availableHooks).mode != CommandMode::Version) {
        printf("Short top-level options did not parse\n");
        return 1;
    }

    CommandLineOptions attach = Parse(
        {L"hooknt.exe",
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
        {L"hooknt.exe", L"attach", L"-p", L"invalid", L"-k", L"all"},
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
        {L"hooknt.exe", L"test.exe", L"NtCreateFile"},
        availableHooks);
    if (legacy.mode != CommandMode::Error) {
        printf("Legacy command was not rejected\n");
        return 1;
    }

    CommandLineOptions terminalOnly = Parse(
        {L"hooknt.exe", L"run", L"--hook", L"NtCreateFile", L"--", L"test.exe"},
        availableHooks);
    if (terminalOnly.mode != CommandMode::Run ||
        terminalOnly.output.quiet ||
        !terminalOnly.output.outputPath.empty()) {
        printf("Terminal-only command did not parse\n");
        return 1;
    }

    CommandLineOptions invalidFormat = Parse(
        {L"hooknt.exe", L"run", L"--hook", L"all", L"--format", L"jsonl", L"--", L"test.exe"},
        availableHooks);
    if (invalidFormat.mode != CommandMode::Error) {
        printf("Format without output path was not rejected\n");
        return 1;
    }

    CommandLineOptions invalidQuiet = Parse(
        {L"hooknt.exe", L"run", L"--hook", L"all", L"--quiet", L"--", L"test.exe"},
        availableHooks);
    if (invalidQuiet.mode != CommandMode::Error) {
        printf("Quiet mode without output path was not rejected\n");
        return 1;
    }

    CommandLineOptions invalidRunPid = Parse(
        {L"hooknt.exe", L"run", L"-p", L"4242", L"-k", L"all", L"--", L"test.exe"},
        availableHooks);
    if (invalidRunPid.mode != CommandMode::Error) {
        printf("Run process ID was not rejected\n");
        return 1;
    }

    return 0;
}
