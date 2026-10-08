#pragma once

// Helpers for tests that run programs installed on the machine (osslsigncode,
// Wine). A check that needs a program is never skipped silently:
//
//   - the program is installed: RequireTool returns true and the check runs;
//   - the program is missing and SEED_TOOLS_OPTIONAL is not "1": the running
//     test fails, naming the program;
//   - the program is missing and SEED_TOOLS_OPTIONAL is "1": the line
//     "NOT RUN: <tool> not installed" is printed and RequireTool returns
//     false, so the caller leaves that check out.
//
// Programs are found on PATH. Only Linux and other POSIX hosts run programs;
// on Windows every tool counts as not installed.

#include "TestPaths.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace seedtest
{

// True when the environment asks to run without the live tools.
inline bool ToolsOptional()
{
    const char *value = std::getenv("SEED_TOOLS_OPTIONAL");
    return value != nullptr && std::string(value) == "1";
}

// Whether an executable with this name is on PATH.
inline bool ToolInstalled(const std::string &name)
{
#ifdef _WIN32
    (void)name;
    return false;
#else
    const char *path = std::getenv("PATH");
    if(path == nullptr)
    {
        return false;
    }
    std::string rest = path;
    std::size_t start = 0;
    while(start <= rest.size())
    {
        std::size_t end = rest.find(':', start);
        if(end == std::string::npos)
        {
            end = rest.size();
        }
        const std::string folder = rest.substr(start, end - start);
        const std::filesystem::path candidate = std::filesystem::path(folder.empty() ? "." : folder) / name;
        std::error_code ec;
        if(std::filesystem::is_regular_file(candidate, ec) && ::access(candidate.c_str(), X_OK) == 0)
        {
            return true;
        }
        start = end + 1;
    }
    return false;
#endif
}

// True when the check may go on. See the header comment.
inline bool RequireTool(const std::string &name)
{
    if(ToolInstalled(name))
    {
        return true;
    }
    if(ToolsOptional())
    {
        std::cout << "NOT RUN: " << name << " not installed" << std::endl;
        return false;
    }
    FAIL(name << " is not installed (set SEED_TOOLS_OPTIONAL=1 to run without it)");
    return false;
}

// One word of a command line, quoted for the shell.
inline std::string ShellQuote(const std::string &text)
{
    std::string out = "'";
    for(const char c : text)
    {
        if(c == '\'')
        {
            out += "'\\''";
        }
        else
        {
            out += c;
        }
    }
    out += "'";
    return out;
}

struct ToolRun
{
    int status = -1;     // exit status; -1 when the program could not be started
    std::string output;  // standard output and standard error together
};

// Runs a command line through the shell and collects what it prints. The
// output goes to a file that is read after the shell returns, not through a
// pipe: Wine leaves its server process running for a few seconds, holding the
// pipe, and reading to the end of a pipe would wait for it.
inline ToolRun RunCommand(const std::string &command)
{
    ToolRun result;
#ifdef _WIN32
    (void)command;
#else
    std::string pattern = (std::filesystem::temp_directory_path() / "seed-tool-output-XXXXXX").string();
    const int descriptor = ::mkstemp(pattern.data());
    if(descriptor < 0)
    {
        return result;
    }
    ::close(descriptor);
    const int raw = std::system(("( " + command + " ) > " + ShellQuote(pattern) + " 2>&1").c_str());
    result.status = (raw != -1 && WIFEXITED(raw)) ? WEXITSTATUS(raw) : -1;
    if(std::FILE *file = std::fopen(pattern.c_str(), "rb"))
    {
        char buffer[4096];
        std::size_t got = 0;
        while((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
        {
            result.output.append(buffer, got);
        }
        std::fclose(file);
    }
    std::error_code ec;
    std::filesystem::remove(pattern, ec);
#endif
    return result;
}

} // namespace seedtest
