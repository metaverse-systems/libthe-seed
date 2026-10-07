#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Reads a whole file. Throws std::runtime_error when it cannot be opened or read.
std::vector<std::uint8_t> ReadFileBytes(const std::string &file_path);

// Number of calls to ReadFileBytes since the process started or since the last
// reset. A call that throws is counted too. Tests use it to check how often a
// file is read; it has no effect on behaviour.
std::uint64_t ReadFileBytesCallCount();
void ResetReadFileBytesCallCount();

// Replaces the content of an existing regular file with `bytes`.
//
// The new content is written to a working file beside the target, flushed and
// then moved over the target, so a reader of the path sees the complete old
// content or the complete new content. On failure the function throws
// std::runtime_error (naming the file, the step and the system reason) and the
// target is unchanged.
//
// What is promised:
//   - After a failure, or after the process is killed part way, the target
//     holds the complete old or the complete new content.
//   - No working file remains after the function returns, on success or on
//     failure. After a kill a working file named `.<name>.seedtmp.<pid>.<hex>`
//     may remain; it is never reused and never touched by a later call.
//   - The working file is created exclusively and without following links, so
//     a file or link that already exists under any name is left alone.
//   - A link is followed: the file it points to is replaced and the link is
//     kept. A missing path, a dangling link, a folder and a special file are
//     refused, as is a read-only file or a read-only folder.
//   - The permission bits (Windows: the hidden, system, archive and
//     not-content-indexed attributes) are those of the original, and are never
//     wider on the working file at any moment. Ownership is copied on a best
//     effort basis.
//   - The data is flushed before the move is requested and the folder entry is
//     flushed after it (Windows: the move is written through). These are
//     requests: they give durability on file systems and devices that honour
//     them.
//
// What is not promised: survival of loss of power on storage that ignores
// flush requests; combining of operations on one file at the same time (the
// last move wins); preservation of extended attributes, ACLs, creation time or
// hard-link identity (the target becomes a new file).
void WriteFileBytes(const std::string &file_path, const std::vector<std::uint8_t> &bytes);
