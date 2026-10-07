// The shared file replacement (WriteFileBytes), called directly on plain files.
//
// What these cases hold the replacement to:
//   - a successful call leaves the new content, the same permission bits and
//     the same folder entries;
//   - a failure at any step throws std::runtime_error whose text names the
//     file, the step and the system reason, and leaves content, mode and
//     folder entries as they were;
//   - flush before the rename and folder flush after it, in that order, and a
//     failing folder flush does not fail the call;
//   - permissions never wider than the original at any moment, ownership copy
//     is best effort;
//   - links are followed and kept, anything that is not a replaceable regular
//     file is refused with nothing changed;
//   - files and links already at the old fixed name or at a name that looks
//     like a working file are never read, written, removed or followed;
//   - concurrent calls leave exactly one complete content and nothing else.
//
// The system calls are replaced through the hook table of
// internal/FileReplaceHooks.hpp. Hooks are plain function pointers, so the
// injected behaviour and what the hooks saw live in the Injection object below.
// Every file is created inside a scratch folder that is removed afterwards.

#include "HeapCounter.hpp"
#include "ReplaceTestSupport.hpp"

#include "internal/FileIO.hpp"
#include "internal/FileReplaceHooks.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <cerrno>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

SEED_DEFINE_HEAP_COUNTER()

namespace {

using namespace seedtest::replace;
namespace fs = std::filesystem;
using seed::internal::FileReplaceHooks;
using seed::internal::FileReplaceHooksScope;

constexpr std::size_t kOldSize = 100000;
constexpr std::size_t kNewSize = 150000;

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// The path the running case is working on. The words of a message are looked
// for outside it, so a word such as "missing" or "replace" in a folder name
// cannot make a message pass.
std::string subject_path;

std::string RemoveAll(std::string text, const std::string &piece)
{
    if(piece.empty())
    {
        return text;
    }
    for(std::size_t at = text.find(piece); at != std::string::npos; at = text.find(piece, at))
    {
        text.erase(at, piece.size());
    }
    return text;
}

bool ContainsAny(const std::string &text, const std::vector<std::string> &words)
{
    const fs::path subject(subject_path);
    const std::string stripped =
        RemoveAll(RemoveAll(text, subject.parent_path().string()), subject.filename().string());
    const std::string lowered = Lower(stripped);
    for(const std::string &word : words)
    {
        if(lowered.find(word) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

// Calls the replacement and returns the text of the error it raised, or fails
// the running case when it did not raise one.
std::string ErrorOf(const std::string &path, const Bytes &bytes)
{
    subject_path = path;
    try
    {
        WriteFileBytes(path, bytes);
    }
    catch(const std::runtime_error &e)
    {
        return e.what();
    }
    catch(...)
    {
        FAIL("WriteFileBytes raised something other than std::runtime_error for " << path);
    }
    FAIL("WriteFileBytes did not refuse " << path);
    return std::string();
}

// A scratch folder holding one file with known content and mode.
struct Subject
{
    seedtest::ScratchDir scratch;
    std::string path;
    Bytes old_content;
    unsigned mode;
    std::vector<FolderEntry> before;

    explicit Subject(unsigned file_mode = 0640)
        : scratch("seed-replace"), path(scratch.File("target.bin")),
          old_content(DistinctContent(1, kOldSize)), mode(file_mode)
    {
        WriteAll(this->path, this->old_content);
        SetModeOf(this->path, this->mode);
        this->before = ListFolder(this->scratch.Path());
    }

    void RequireUntouched() const
    {
        CHECK(ReadAll(this->path) == this->old_content);
        CHECK(ModeOf(this->path) == this->mode);
        const auto after = ListFolder(this->scratch.Path());
        INFO("before: " << Describe(this->before) << "\nafter: " << Describe(after));
        CHECK(after == this->before);
    }
};

std::vector<std::string> Names(const std::vector<FolderEntry> &entries)
{
    std::vector<std::string> names;
    for(const FolderEntry &entry : entries)
    {
        names.push_back(entry.name);
    }
    return names;
}

// Both tests of "what the message says" use the same rule: it names the file
// by its last path component.
void RequireNames(const std::string &message, const std::string &path)
{
    INFO("message: " << message);
    CHECK(message.find(fs::path(path).filename().string()) != std::string::npos);
}

#if !defined(_WIN32)

// ---------------------------------------------------------------------------
// Injection through the hook table (single-threaded use only)
// ---------------------------------------------------------------------------

struct Injection
{
    // Which step fails: "open", "write", "fchmod", "fchown", "fsync", "rename",
    // "fsync_folder", or empty for none.
    std::string stage;
    int err = 0;
    // For "write": bytes accepted before the failure.
    long write_limit = -1;
    // For the working-file open: fail this many opens with EEXIST (or all).
    int open_exist_times = 0;
    bool open_exist_always = false;

    // What the hooks saw.
    int hits = 0;
    int fchown_calls = 0;
    int unlink_calls = 0;
    long written = 0;
    std::vector<std::string> open_paths;
    std::vector<int> open_flags;
    std::vector<unsigned> open_modes;
    std::vector<unsigned> fchmod_modes;
    std::string work_path;
    std::vector<unsigned> observed_modes;
};

Injection g;

bool IsWorkingName(const char *path)
{
    return std::strstr(path, ".seedtmp.") != nullptr;
}

// Mode of the working file at this moment (it exists once the open succeeded).
void Observe()
{
    if(g.work_path.empty())
    {
        return;
    }
    struct stat info;
    if(::lstat(g.work_path.c_str(), &info) == 0)
    {
        g.observed_modes.push_back(static_cast<unsigned>(info.st_mode & 07777));
    }
}

bool Fails(const char *stage)
{
    if(g.stage == stage)
    {
        ++g.hits;
        errno = g.err;
        return true;
    }
    return false;
}

int HookOpen(const char *path, int flags, mode_t mode)
{
    if(!IsWorkingName(path))
    {
        return ::open(path, flags, mode);
    }
    g.open_paths.push_back(path);
    g.open_flags.push_back(flags);
    g.open_modes.push_back(static_cast<unsigned>(mode));
    if(Fails("open"))
    {
        return -1;
    }
    if(g.open_exist_always || g.open_exist_times > 0)
    {
        if(g.open_exist_times > 0)
        {
            --g.open_exist_times;
        }
        // Give up after a lot of attempts so a missing bound cannot hang the
        // run: the real call goes through and the case then fails on the count.
        if(g.open_paths.size() < 2000)
        {
            errno = EEXIST;
            return -1;
        }
    }
    const int fd = ::open(path, flags, mode);
    if(fd >= 0)
    {
        g.work_path = path;
    }
    Observe();
    return fd;
}

ssize_t HookWrite(int fd, const void *data, std::size_t size)
{
    Observe();
    if(g.stage == "write" && g.write_limit >= 0)
    {
        const long remaining = g.write_limit - g.written;
        if(remaining <= 0)
        {
            ++g.hits;
            errno = g.err;
            return -1;
        }
        const std::size_t part = std::min<std::size_t>(size, static_cast<std::size_t>(remaining));
        const ssize_t result = ::write(fd, data, part);
        if(result > 0)
        {
            g.written += result;
        }
        return result;
    }
    return ::write(fd, data, size);
}

int HookFchmod(int fd, mode_t mode)
{
    Observe();
    g.fchmod_modes.push_back(static_cast<unsigned>(mode));
    if(Fails("fchmod"))
    {
        return -1;
    }
    const int result = ::fchmod(fd, mode);
    Observe();
    return result;
}

int HookFchown(int fd, uid_t owner, gid_t group)
{
    Observe();
    ++g.fchown_calls;
    if(Fails("fchown"))
    {
        return -1;
    }
    return ::fchown(fd, owner, group);
}

int HookFsync(int fd)
{
    Observe();
    if(Fails("fsync"))
    {
        return -1;
    }
    return ::fsync(fd);
}

int HookClose(int fd)
{
    Observe();
    return ::close(fd);
}

int HookRename(const char *from, const char *to)
{
    Observe();
    if(Fails("rename"))
    {
        return -1;
    }
    return ::rename(from, to);
}

int HookUnlink(const char *path)
{
    ++g.unlink_calls;
    return ::unlink(path);
}

int HookFsyncFolder(const char *folder)
{
    if(g.stage == "fsync_folder")
    {
        ++g.hits;
        return g.err;
    }
    static const FileReplaceHooks real = seed::internal::DefaultFileReplaceHooks();
    return real.fsync_folder(folder);
}

FileReplaceHooks InjectingHooks()
{
    FileReplaceHooks hooks = seed::internal::DefaultFileReplaceHooks();
    hooks.open = &HookOpen;
    hooks.write = &HookWrite;
    hooks.fchmod = &HookFchmod;
    hooks.fchown = &HookFchown;
    hooks.fsync = &HookFsync;
    hooks.close = &HookClose;
    hooks.rename = &HookRename;
    hooks.unlink = &HookUnlink;
    hooks.fsync_folder = &HookFsyncFolder;
    return hooks;
}

// One failure case: the step `stage` fails with `err`; the call must raise an
// error that names the file, says which step failed and gives the system
// reason, and must leave everything as it was.
void RequireStageFailure(const std::string &stage, int err, long write_limit,
                         const std::vector<std::string> &step_words)
{
    Subject subject;
    const Bytes replacement = DistinctContent(2, kNewSize);

    g = Injection{};
    g.stage = stage;
    g.err = err;
    g.write_limit = write_limit;

    std::string message;
    subject_path = subject.path;
    {
        FileReplaceHooksScope scope(InjectingHooks());
        message = ErrorOf(subject.path, replacement);
    }
    INFO("stage " << stage << ", message: " << message);
    CHECK(g.hits > 0);
    RequireNames(message, subject.path);
    CHECK(Lower(message).find(Lower(std::strerror(err))) != std::string::npos);
    CHECK(ContainsAny(message, step_words));
    subject.RequireUntouched();
}

#endif

}

TEST_CASE("replace: success gives the new content, the same mode and the same folder entries",
          "[FileReplace][success]")
{
    Subject subject(0640);
    const Bytes replacement = DistinctContent(2, kNewSize);

    REQUIRE_NOTHROW(WriteFileBytes(subject.path, replacement));

    CHECK(ReadAll(subject.path) == replacement);
    const auto after = ListFolder(subject.scratch.Path());
    INFO("before: " << Describe(subject.before) << "\nafter: " << Describe(after));
    CHECK(Names(after) == Names(subject.before));
}

TEST_CASE("replace: an empty buffer and a buffer smaller than one page are replaced whole",
          "[FileReplace][success]")
{
    Subject subject;
    const Bytes small = {1, 2, 3};
    REQUIRE_NOTHROW(WriteFileBytes(subject.path, small));
    CHECK(ReadAll(subject.path) == small);

    REQUIRE_NOTHROW(WriteFileBytes(subject.path, Bytes()));
    CHECK(ReadAll(subject.path).empty());
    CHECK(Names(ListFolder(subject.scratch.Path())) == Names(subject.before));
}

TEST_CASE("replace: a large buffer is written without a second full copy", "[FileReplace][memory]")
{
    constexpr std::size_t kLarge = 48ull * 1024 * 1024;
    constexpr std::size_t kAllowance = 4ull * 1024 * 1024;

    Subject subject;
    Bytes large(kLarge);
    for(std::size_t i = 0; i < large.size(); ++i)
    {
        large[i] = static_cast<std::uint8_t>(i * 31 + (i >> 9));
    }

    std::size_t growth = 0;
    {
        seedtest::heap::HeapGrowthScope heap;
        REQUIRE_NOTHROW(WriteFileBytes(subject.path, large));
        growth = heap.PeakGrowth();
    }

    REQUIRE(seedtest::heap::HeapGrowthScope::Active());
    INFO("peak heap growth during the call: " << growth << " bytes for a " << kLarge
                                              << " byte buffer");
    CHECK(growth <= kAllowance);
    CHECK(ReadAll(subject.path) == large);
    CHECK(Names(ListFolder(subject.scratch.Path())) == Names(subject.before));
}

#if !defined(_WIN32)

TEST_CASE("replace: failure at each step leaves content, mode and folder entries unchanged",
          "[FileReplace][failure]")
{
    SECTION("the working file cannot be created")
    {
        RequireStageFailure("open", EACCES, -1, {"create", "open", "working"});
    }
    SECTION("the write fails part-way")
    {
        RequireStageFailure("write", EIO, 50000, {"write"});
    }
    SECTION("the file system fills up after 70000 bytes")
    {
        RequireStageFailure("write", ENOSPC, 70000, {"write"});
    }
    SECTION("the write fails at once")
    {
        RequireStageFailure("write", ENOSPC, 0, {"write"});
    }
    SECTION("fchmod fails")
    {
        RequireStageFailure("fchmod", EPERM, -1, {"mode", "permission", "chmod"});
    }
    SECTION("fsync fails")
    {
        RequireStageFailure("fsync", EIO, -1, {"flush", "sync"});
    }
    SECTION("rename fails")
    {
        RequireStageFailure("rename", EACCES, -1, {"rename", "replace", "move"});
    }
}

TEST_CASE("replace: flush before the rename, folder flush after it", "[FileReplace][order]")
{
    Subject subject;
    const Bytes replacement = DistinctContent(2, kNewSize);

    FileReplaceHooksScope scope;
    REQUIRE_NOTHROW(WriteFileBytes(subject.path, replacement));
    const auto calls = scope.Calls();

    auto first = [&](const char *name) -> long {
        for(std::size_t i = 0; i < calls.size(); ++i)
        {
            if(calls[i].name == name)
            {
                return static_cast<long>(i);
            }
        }
        return -1;
    };
    std::string sequence;
    for(const auto &call : calls)
    {
        sequence += call.name + " ";
    }
    INFO("recorded calls: " << sequence);

    const long open_at = first("open");
    const long write_at = first("write");
    const long fsync_at = first("fsync");
    const long close_at = first("close");
    const long rename_at = first("rename");
    const long folder_at = first("fsync_folder");
    REQUIRE(open_at >= 0);
    REQUIRE(write_at >= 0);
    REQUIRE(fsync_at >= 0);
    REQUIRE(close_at >= 0);
    REQUIRE(rename_at >= 0);
    REQUIRE(folder_at >= 0);
    CHECK(open_at < write_at);
    CHECK(write_at < fsync_at);
    CHECK(fsync_at < rename_at);
    CHECK(close_at < rename_at);
    CHECK(rename_at < folder_at);
    CHECK(first("unlink") == -1);
    CHECK(ReadAll(subject.path) == replacement);
}

TEST_CASE("replace: a failing folder flush does not fail the operation", "[FileReplace][order]")
{
    Subject subject;
    const Bytes replacement = DistinctContent(2, kNewSize);

    g = Injection{};
    g.stage = "fsync_folder";
    g.err = EIO;
    {
        FileReplaceHooksScope scope(InjectingHooks());
        REQUIRE_NOTHROW(WriteFileBytes(subject.path, replacement));
    }
    CHECK(g.hits > 0);
    CHECK(ReadAll(subject.path) == replacement);
    CHECK(ModeOf(subject.path) == subject.mode);
    CHECK(Names(ListFolder(subject.scratch.Path())) == Names(subject.before));
}

TEST_CASE("replace: permissions are the same after a replacement", "[FileReplace][permissions]")
{
    const unsigned plain_modes[] = {0755, 0700, 0640, 0600};
    for(const unsigned mode : plain_modes)
    {
        Subject subject(mode);
        INFO("mode " << std::oct << mode);
        REQUIRE_NOTHROW(WriteFileBytes(subject.path, DistinctContent(2, 5000)));
        CHECK(ModeOf(subject.path) == mode);
        CHECK(ReadAll(subject.path) == DistinctContent(2, 5000));
        CHECK(Names(ListFolder(subject.scratch.Path())) == Names(subject.before));
    }

    const unsigned special_modes[] = {02755, 01755, 03755};
    for(const unsigned mode : special_modes)
    {
        Subject subject(0755);
        SetModeOf(subject.path, mode);
        if(ModeOf(subject.path) != mode)
        {
            SkipWithMessage("special permission bits " + std::to_string(mode) +
                            " cannot be set here (file system or account)");
            continue;
        }
        INFO("mode " << std::oct << mode);
        REQUIRE_NOTHROW(WriteFileBytes(subject.path, DistinctContent(2, 5000)));
        CHECK(ModeOf(subject.path) == mode);
    }
}

TEST_CASE("replace: the working file is born with mode 0 and never wider than the original",
          "[FileReplace][permissions]")
{
    Subject subject(0640);
    const Bytes replacement = DistinctContent(2, kNewSize);

    g = Injection{};
    {
        FileReplaceHooksScope scope(InjectingHooks());
        REQUIRE_NOTHROW(WriteFileBytes(subject.path, replacement));
    }

    REQUIRE(g.open_paths.size() == 1);
    CHECK(g.open_modes[0] == 0);
    CHECK((g.open_flags[0] & O_EXCL) != 0);
    CHECK((g.open_flags[0] & O_CREAT) != 0);
    CHECK((g.open_flags[0] & O_NOFOLLOW) != 0);
    // beside the target, hidden, pattern .<name>.seedtmp.<pid>.<hex>
    const fs::path working(g.open_paths[0]);
    CHECK(working.parent_path() == fs::path(subject.path).parent_path());
    CHECK(working.filename().string().rfind(".target.bin.seedtmp.", 0) == 0);

    REQUIRE_FALSE(g.observed_modes.empty());
    for(const unsigned seen : g.observed_modes)
    {
        INFO("a step saw mode " << std::oct << seen);
        CHECK((seen & ~subject.mode) == 0);
    }
    REQUIRE_FALSE(g.fchmod_modes.empty());
    CHECK(g.fchmod_modes.back() == subject.mode);
    CHECK(ModeOf(subject.path) == subject.mode);
}

TEST_CASE("replace: a failing ownership copy does not fail the operation", "[FileReplace][permissions]")
{
    Subject subject(0640);
    const Bytes replacement = DistinctContent(2, kNewSize);

    g = Injection{};
    g.stage = "fchown";
    g.err = EPERM;
    {
        FileReplaceHooksScope scope(InjectingHooks());
        REQUIRE_NOTHROW(WriteFileBytes(subject.path, replacement));
    }
    CHECK(g.fchown_calls > 0);
    CHECK(g.hits > 0);
    CHECK(ReadAll(subject.path) == replacement);
    CHECK(ModeOf(subject.path) == 0640);
    CHECK(Names(ListFolder(subject.scratch.Path())) == Names(subject.before));
}

TEST_CASE("replace: a read-only file is refused with a message saying how to proceed",
          "[FileReplace][permissions][refusal]")
{
    if(RunningAsRoot())
    {
        SkipWithMessage("read-only file is not refused for the administrator account; "
                        "the permission-denied cases need a normal user");
        return;
    }
    Subject subject(0444);
    const std::string message = ErrorOf(subject.path, DistinctContent(2, 5000));
    RequireNames(message, subject.path);
    CHECK(Lower(message).find("read-only") != std::string::npos);
    CHECK(ContainsAny(message, {"chmod", "writable", "write permission", "permission"}));
    subject.RequireUntouched();
}

TEST_CASE("replace: a link is followed, kept and the target keeps its mode", "[FileReplace][link]")
{
    seedtest::ScratchDir scratch("seed-replace-link");
    const fs::path real_dir = scratch.Path() / "real";
    const fs::path link_dir = scratch.Path() / "links";
    fs::create_directory(real_dir);
    fs::create_directory(link_dir);

    const std::string target = (real_dir / "real.bin").string();
    const std::string link = (link_dir / "link.bin").string();
    const Bytes old_content = DistinctContent(1, 20000);
    const Bytes replacement = DistinctContent(2, 30000);
    WriteAll(target, old_content);
    SetModeOf(target, 0640);
    fs::create_symlink("../real/real.bin", link);

    const auto real_before = Names(ListFolder(real_dir));
    const auto links_before = ListFolder(link_dir);

    REQUIRE_NOTHROW(WriteFileBytes(link, replacement));

    CHECK(fs::is_symlink(fs::symlink_status(link)));
    CHECK(fs::read_symlink(link) == fs::path("../real/real.bin"));
    CHECK(ReadAll(target) == replacement);
    CHECK(ReadAll(link) == replacement);
    CHECK(ModeOf(target) == 0640);
    CHECK(Names(ListFolder(real_dir)) == real_before);
    CHECK(ListFolder(link_dir) == links_before);
}

TEST_CASE("replace: a dangling link, a folder, a FIFO and a missing path are refused",
          "[FileReplace][refusal]")
{
    seedtest::ScratchDir scratch("seed-replace-refuse");
    const Bytes replacement = DistinctContent(2, 5000);

    const std::string dangling = scratch.File("dangling.bin");
    fs::create_symlink("nowhere.bin", dangling);
    const std::string folder = scratch.File("folder.bin");
    fs::create_directory(folder);
    const std::string fifo = scratch.File("fifo.bin");
    REQUIRE(::mkfifo(fifo.c_str(), 0644) == 0);
    const std::string missing = scratch.File("missing.bin");

    const auto before = ListFolder(scratch.Path());

    SECTION("dangling link")
    {
        const std::string message = ErrorOf(dangling, replacement);
        RequireNames(message, dangling);
        CHECK(ContainsAny(message, {"dangling", "no such file", "does not exist", "missing"}));
        CHECK(fs::read_symlink(dangling) == fs::path("nowhere.bin"));
    }
    SECTION("folder")
    {
        const std::string message = ErrorOf(folder, replacement);
        RequireNames(message, folder);
        CHECK(ContainsAny(message, {"directory", "folder", "not a regular file"}));
        CHECK(fs::is_directory(folder));
    }
    SECTION("FIFO")
    {
        const std::string message = ErrorOf(fifo, replacement);
        RequireNames(message, fifo);
        CHECK(ContainsAny(message, {"not a regular file", "special", "fifo", "pipe"}));
    }
    SECTION("missing path")
    {
        const std::string message = ErrorOf(missing, replacement);
        RequireNames(message, missing);
        CHECK(ContainsAny(message, {"no such file", "does not exist", "missing", "not found"}));
    }

    const auto after = ListFolder(scratch.Path());
    INFO("before: " << Describe(before) << "\nafter: " << Describe(after));
    CHECK(after == before);
}

TEST_CASE("replace: a read-only folder is refused and nothing is left behind",
          "[FileReplace][permissions][refusal]")
{
    if(RunningAsRoot())
    {
        SkipWithMessage("read-only folder is not refused for the administrator account; "
                        "the permission-denied cases need a normal user");
        return;
    }
    seedtest::ScratchDir scratch("seed-replace-ro");
    const fs::path folder = scratch.Path() / "locked";
    fs::create_directory(folder);
    const std::string target = (folder / "target.bin").string();
    WriteAll(target, DistinctContent(1, 5000));
    const auto before = ListFolder(folder);

    SetModeOf(folder.string(), 0555);
    std::string message;
    try
    {
        message = ErrorOf(target, DistinctContent(2, 5000));
    }
    catch(...)
    {
        SetModeOf(folder.string(), 0755);
        throw;
    }
    const auto after = ListFolder(folder);
    SetModeOf(folder.string(), 0755);

    RequireNames(message, target);
    CHECK(ContainsAny(message, {"folder", "directory"}));
    CHECK(after == before);
    CHECK(ReadAll(target) == DistinctContent(1, 5000));
}

#endif

TEST_CASE("replace: files at the old fixed name are neither used nor changed", "[FileReplace][leftover]")
{
    Subject subject;
    const Bytes stale = DistinctContent(7, 3000);
    const std::string old_name = subject.path + ".tmp";
    WriteAll(old_name, stale);
    const Bytes replacement = DistinctContent(2, kNewSize);

    REQUIRE_NOTHROW(WriteFileBytes(subject.path, replacement));

    CHECK(ReadAll(subject.path) == replacement);
    CHECK(ReadAll(old_name) == stale);
    const auto names = Names(ListFolder(subject.scratch.Path()));
    CHECK(names == std::vector<std::string>{"target.bin", "target.bin.tmp"});
}

#if !defined(_WIN32)

TEST_CASE("replace: a link planted at the old fixed name is not followed", "[FileReplace][leftover]")
{
    Subject subject;
    seedtest::ScratchDir other("seed-replace-victim");
    const std::string victim = other.File("victim.bin");
    const Bytes victim_content = DistinctContent(9, 4000);
    WriteAll(victim, victim_content);
    SetModeOf(victim, 0600);
    fs::create_symlink(victim, subject.path + ".tmp");

    const Bytes replacement = DistinctContent(2, kNewSize);
    REQUIRE_NOTHROW(WriteFileBytes(subject.path, replacement));

    CHECK(ReadAll(subject.path) == replacement);
    CHECK(ReadAll(victim) == victim_content);
    CHECK(ModeOf(victim) == 0600);
    CHECK(fs::is_symlink(fs::symlink_status(subject.path + ".tmp")));
    CHECK(fs::read_symlink(subject.path + ".tmp") == fs::path(victim));
    CHECK(Names(ListFolder(subject.scratch.Path())) ==
          std::vector<std::string>{"target.bin", "target.bin.tmp"});
}

TEST_CASE("replace: files that look like working files are left alone", "[FileReplace][leftover]")
{
    Subject subject;
    seedtest::ScratchDir other("seed-replace-victim");
    const std::string victim = other.File("victim.bin");
    const Bytes victim_content = DistinctContent(9, 4000);
    WriteAll(victim, victim_content);

    const Bytes stale_a = DistinctContent(7, 1000);
    const Bytes stale_b = DistinctContent(8, 2000);
    const std::string pid = std::to_string(static_cast<long>(::getpid()));
    const std::string a = subject.scratch.File(".target.bin.seedtmp.1.0000000000000000");
    const std::string b = subject.scratch.File(".target.bin.seedtmp." + pid + ".0000000000000000");
    const std::string c = subject.scratch.File(".target.bin.seedtmp.2.ffffffffffffffff");
    WriteAll(a, stale_a);
    WriteAll(b, stale_b);
    fs::create_symlink(victim, c);

    const Bytes replacement = DistinctContent(2, kNewSize);
    REQUIRE_NOTHROW(WriteFileBytes(subject.path, replacement));

    CHECK(ReadAll(subject.path) == replacement);
    CHECK(ReadAll(a) == stale_a);
    CHECK(ReadAll(b) == stale_b);
    CHECK(fs::is_symlink(fs::symlink_status(c)));
    CHECK(fs::read_symlink(c) == fs::path(victim));
    CHECK(ReadAll(victim) == victim_content);
    CHECK(Names(ListFolder(subject.scratch.Path())).size() == 4);
}

TEST_CASE("replace: a working name that exists is retried with a new name", "[FileReplace][leftover]")
{
    Subject subject;
    const Bytes replacement = DistinctContent(2, kNewSize);

    g = Injection{};
    g.open_exist_times = 3;
    {
        FileReplaceHooksScope scope(InjectingHooks());
        REQUIRE_NOTHROW(WriteFileBytes(subject.path, replacement));
    }

    REQUIRE(g.open_paths.size() == 4);
    std::vector<std::string> unique = g.open_paths;
    std::sort(unique.begin(), unique.end());
    unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
    CHECK(unique.size() == 4);
    CHECK(ReadAll(subject.path) == replacement);
    CHECK(Names(ListFolder(subject.scratch.Path())) == Names(subject.before));
}

TEST_CASE("replace: a bounded number of name clashes ends in an error with nothing left",
          "[FileReplace][leftover]")
{
    Subject subject;

    g = Injection{};
    g.open_exist_always = true;
    std::string message;
    {
        FileReplaceHooksScope scope(InjectingHooks());
        message = ErrorOf(subject.path, DistinctContent(2, kNewSize));
    }

    RequireNames(message, subject.path);
    CHECK(Lower(message).find(Lower(std::strerror(EEXIST))) != std::string::npos);
    CHECK(g.open_paths.size() >= 2);
    CHECK(g.open_paths.size() <= 1000);
    subject.RequireUntouched();
}

#endif

// ---------------------------------------------------------------------------
// Concurrency
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kConcurrentSize = 32768;

struct RoundFailure
{
    std::mutex lock;
    std::string first_error;
    std::atomic<int> errors{0};

    void Record(const std::string &what)
    {
        std::lock_guard<std::mutex> guard(this->lock);
        if(this->errors.fetch_add(1) == 0)
        {
            this->first_error = what;
        }
    }
};

void RunThreadRounds(unsigned workers, unsigned rounds)
{
    seedtest::ScratchDir scratch("seed-replace-threads");
    const std::string path = scratch.File("shared.bin");
    WriteAll(path, DistinctContent(0, kConcurrentSize));
    const auto original = ListFolder(scratch.Path());

    std::vector<Bytes> contents;
    for(unsigned i = 0; i <= workers; ++i)
    {
        contents.push_back(DistinctContent(i, kConcurrentSize));
    }

    for(unsigned round = 0; round < rounds; ++round)
    {
        RoundFailure failure;
        std::atomic<bool> go{false};
        std::vector<std::thread> threads;
        for(unsigned i = 1; i <= workers; ++i)
        {
            threads.emplace_back([&, i] {
                while(!go.load())
                {
                    std::this_thread::yield();
                }
                try
                {
                    WriteFileBytes(path, contents[i]);
                }
                catch(const std::exception &e)
                {
                    failure.Record(e.what());
                }
            });
        }
        go.store(true);
        for(std::thread &thread : threads)
        {
            thread.join();
        }

        const Bytes now = ReadAll(path);
        const long which = IdentifyContent(now, workers + 1);
        const auto listing = ListFolder(scratch.Path());
        const bool same_listing = Names(listing) == Names(original);
        if(failure.errors.load() != 0 || which < 0 || !same_listing)
        {
            FAIL("round " << round << " with " << workers << " threads: errors "
                          << failure.errors.load() << " (" << failure.first_error
                          << "), content identified as " << which << ", folder: "
                          << Describe(listing));
        }
    }
}

}

TEST_CASE("replace: concurrent threads on one file leave exactly one complete content",
          "[FileReplace][concurrency]")
{
    const unsigned workers = GENERATE(2u, 4u, 8u);
    RunThreadRounds(workers, 1000);
}

TEST_CASE("replace: two files in one folder replaced at the same time", "[FileReplace][concurrency]")
{
    seedtest::ScratchDir scratch("seed-replace-two");
    const std::string first = scratch.File("first.bin");
    const std::string second = scratch.File("second.bin");
    WriteAll(first, DistinctContent(0, kConcurrentSize));
    WriteAll(second, DistinctContent(0, kConcurrentSize));
    const auto original = ListFolder(scratch.Path());

    RoundFailure failure;
    auto work = [&](const std::string &path, std::uint64_t base) {
        for(std::uint64_t round = 0; round < 1000; ++round)
        {
            const Bytes content = DistinctContent(base + round, kConcurrentSize);
            try
            {
                WriteFileBytes(path, content);
                if(ReadAll(path) != content)
                {
                    failure.Record("content differs after replacing " + path);
                }
            }
            catch(const std::exception &e)
            {
                failure.Record(e.what());
            }
        }
    };
    std::thread a(work, first, 1000);
    std::thread b(work, second, 5000);
    a.join();
    b.join();

    const auto listing = ListFolder(scratch.Path());
    INFO("first error: " << failure.first_error << "\nfolder: " << Describe(listing));
    CHECK(failure.errors.load() == 0);
    CHECK(Names(listing) == Names(original));
}

#if !defined(_WIN32)

TEST_CASE("replace: concurrent processes on one file leave exactly one complete content",
          "[FileReplace][concurrency]")
{
    const unsigned workers = GENERATE(2u, 4u, 8u);
    seedtest::ScratchDir scratch("seed-replace-procs");
    const std::string path = scratch.File("shared.bin");
    WriteAll(path, DistinctContent(0, kConcurrentSize));
    const auto original = ListFolder(scratch.Path());

    std::vector<Bytes> contents;
    for(unsigned i = 0; i <= workers; ++i)
    {
        contents.push_back(DistinctContent(i, kConcurrentSize));
    }

    std::vector<pid_t> children;
    for(unsigned i = 1; i <= workers; ++i)
    {
        const pid_t pid = ::fork();
        REQUIRE(pid >= 0);
        if(pid == 0)
        {
            int status = 0;
            for(unsigned round = 0; round < 1000 && status == 0; ++round)
            {
                try
                {
                    WriteFileBytes(path, contents[i]);
                }
                catch(...)
                {
                    status = 1;
                }
            }
            ::_exit(status);
        }
        children.push_back(pid);
    }

    // While the children run, every read must give exactly one content.
    long bad_reads = 0;
    long reads = 0;
    std::size_t remaining = children.size();
    int failed_children = 0;
    std::vector<bool> done(children.size(), false);
    while(remaining > 0)
    {
        const Bytes now = ReadAll(path);
        ++reads;
        if(IdentifyContent(now, workers + 1) < 0)
        {
            ++bad_reads;
        }
        for(std::size_t i = 0; i < children.size(); ++i)
        {
            if(done[i])
            {
                continue;
            }
            int status = 0;
            const pid_t result = ::waitpid(children[i], &status, WNOHANG);
            if(result == children[i])
            {
                done[i] = true;
                --remaining;
                if(!WIFEXITED(status) || WEXITSTATUS(status) != 0)
                {
                    ++failed_children;
                }
            }
        }
    }

    const auto listing = ListFolder(scratch.Path());
    INFO(workers << " processes: " << reads << " reads, " << bad_reads << " mixed or unknown, "
                 << failed_children << " processes reported an error; folder: "
                 << Describe(listing));
    CHECK(bad_reads == 0);
    CHECK(failed_children == 0);
    CHECK(IdentifyContent(ReadAll(path), workers + 1) >= 0);
    CHECK(Names(listing) == Names(original));
}

#endif

#if defined(_WIN32)

TEST_CASE("replace (Windows): attributes are kept, read-only is refused, old fixed name untouched",
          "[FileReplace][windows]")
{
    Subject subject;
    const std::wstring wide = fs::path(subject.path).wstring();
    constexpr DWORD kWanted = FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_ARCHIVE |
                              FILE_ATTRIBUTE_NOT_CONTENT_INDEXED;

    SECTION("hidden, archive and not-content-indexed survive")
    {
        REQUIRE(::SetFileAttributesW(wide.c_str(), kWanted));
        // Only the attributes the file system actually holds can be compared
        // (wine does not store not-content-indexed).
        const DWORD kKept = ::GetFileAttributesW(wide.c_str()) & kWanted;
        if(kKept != kWanted)
        {
            SkipWithMessage("this file system does not keep every one of hidden, archive and "
                            "not-content-indexed; the ones it keeps are compared");
        }
        REQUIRE((kKept & FILE_ATTRIBUTE_HIDDEN) != 0);
        const Bytes replacement = DistinctContent(2, kNewSize);
        REQUIRE_NOTHROW(WriteFileBytes(subject.path, replacement));
        const DWORD after = ::GetFileAttributesW(wide.c_str());
        REQUIRE(after != INVALID_FILE_ATTRIBUTES);
        CHECK((after & kKept) == kKept);
        CHECK(ReadAll(subject.path) == replacement);
        CHECK(Names(ListFolder(subject.scratch.Path())) == Names(subject.before));
        ::SetFileAttributesW(wide.c_str(), FILE_ATTRIBUTE_NORMAL);
    }
    SECTION("read-only is refused with a message about clearing the attribute")
    {
        REQUIRE(::SetFileAttributesW(wide.c_str(), FILE_ATTRIBUTE_READONLY));
        std::string message;
        try
        {
            message = ErrorOf(subject.path, DistinctContent(2, 5000));
        }
        catch(...)
        {
            ::SetFileAttributesW(wide.c_str(), FILE_ATTRIBUTE_NORMAL);
            throw;
        }
        RequireNames(message, subject.path);
        CHECK(Lower(message).find("read-only") != std::string::npos);
        CHECK(Lower(message).find("attribute") != std::string::npos);
        CHECK(ReadAll(subject.path) == subject.old_content);
        CHECK(Names(ListFolder(subject.scratch.Path())) == Names(subject.before));
        ::SetFileAttributesW(wide.c_str(), FILE_ATTRIBUTE_NORMAL);
    }
    SECTION("the order of flush and move")
    {
        FileReplaceHooksScope scope;
        REQUIRE_NOTHROW(WriteFileBytes(subject.path, DistinctContent(2, kNewSize)));
        long flush_at = -1;
        long move_at = -1;
        const auto calls = scope.Calls();
        for(std::size_t i = 0; i < calls.size(); ++i)
        {
            const std::string name = Lower(calls[i].name);
            if(flush_at < 0 && name.find("flush") != std::string::npos)
            {
                flush_at = static_cast<long>(i);
            }
            if(move_at < 0 && name.find("move") != std::string::npos)
            {
                move_at = static_cast<long>(i);
            }
        }
        REQUIRE(flush_at >= 0);
        REQUIRE(move_at >= 0);
        CHECK(flush_at < move_at);
    }
}

TEST_CASE("replace (Windows): cases wine cannot show truthfully", "[FileReplace][windows]")
{
    SkipWithMessage("replacing a file another process holds open: sharing violations are not "
                    "reproduced faithfully by wine");
    SkipWithMessage("durability after loss of power: not observable from a test; the order of "
                    "flush and write-through move is checked instead");
}

#endif
