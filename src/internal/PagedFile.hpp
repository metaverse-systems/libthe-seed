#pragma once

// A file read in pages, for questions that touch only a few places in it.
//
// The file is opened once and a 64 KiB page is read the first time a byte in
// it is asked for. Pages stay for the life of the object, so what is held is
// bounded by what the caller touched, never by the size of the file. The
// object belongs to one query: it is created, read through a ByteSpan and
// destroyed, and nothing is kept between queries. It is not safe to use from
// two threads at once.
//
// The size is the size of the file when it was opened, so every bounds check
// made against it is the same as for a buffer holding the whole file. A page
// the system can no longer supply (the file was cut short after it was opened)
// is the same std::runtime_error as a whole-file read that fails.

#include "RandomAccessFile.hpp"

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <string>
#include <vector>

namespace the_seed::internal
{

class PagedFile
{
  public:
    static constexpr std::uint64_t kPageSize = std::uint64_t{64} * 1024;

    // Opens the file. Throws std::runtime_error: "Unable to open file: <path>"
    // or "Unable to read file size: <path>", as ReadFileBytes does.
    explicit PagedFile(const std::string &file_path);

    PagedFile(const PagedFile &) = delete;
    PagedFile &operator=(const PagedFile &) = delete;

    std::uint64_t Size() const
    {
        return this->size;
    }

    // A pointer to `length` contiguous bytes at `offset`, which must lie inside
    // the file. Bytes that cross a page boundary are copied together. The
    // pointer stays valid until the object is destroyed. Throws
    // std::runtime_error ("Unable to read file: <path>") when a page cannot be
    // read.
    const std::uint8_t *Ensure(std::uint64_t offset, std::uint64_t length) const;

  private:
    const std::uint8_t *Page(std::uint64_t index) const;

    std::string path;
    RandomAccessFile file;
    std::uint64_t size = 0;
    mutable std::map<std::uint64_t, std::vector<std::uint8_t>> pages;
    // Ranges that crossed a page boundary, each kept so earlier pointers stay
    // valid.
    mutable std::list<std::vector<std::uint8_t>> joined;
};

} // namespace the_seed::internal
