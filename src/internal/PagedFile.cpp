#include "PagedFile.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace the_seed::internal
{

namespace
{

RandomAccessFile OpenFile(const std::string &file_path)
{
    try
    {
        return RandomAccessFile::Open(file_path);
    }
    catch(const FileOpenError &)
    {
        throw std::runtime_error("Unable to open file: " + file_path);
    }
}

} // namespace

PagedFile::PagedFile(const std::string &file_path) : path(file_path), file(OpenFile(file_path))
{
    try
    {
        this->size = this->file.Size();
    }
    catch(const FileReadError &)
    {
        throw std::runtime_error("Unable to read file size: " + file_path);
    }
}

const std::uint8_t *PagedFile::Page(std::uint64_t index) const
{
    const auto found = this->pages.find(index);
    if(found != this->pages.end())
    {
        return found->second.data();
    }

    const std::uint64_t start = index * kPageSize;
    const std::uint64_t length = std::min(kPageSize, this->size - start);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    try
    {
        this->file.ReadAt(start, bytes.data(), length);
    }
    catch(const std::runtime_error &)
    {
        throw std::runtime_error("Unable to read file: " + this->path);
    }
    return this->pages.emplace(index, std::move(bytes)).first->second.data();
}

const std::uint8_t *PagedFile::Ensure(std::uint64_t offset, std::uint64_t length) const
{
    if(length == 0)
    {
        // Nothing is read.
        static const std::uint8_t none = 0;
        return &none;
    }

    const std::uint64_t first = offset / kPageSize;
    const std::uint64_t last = (offset + length - 1) / kPageSize;
    if(first == last)
    {
        return this->Page(first) + (offset - first * kPageSize);
    }

    std::vector<std::uint8_t> together(static_cast<std::size_t>(length));
    std::uint64_t done = 0;
    for(std::uint64_t index = first; index <= last; ++index)
    {
        const std::uint8_t *page = this->Page(index);
        const std::uint64_t page_start = index * kPageSize;
        const std::uint64_t from = std::max(offset, page_start) - page_start;
        const std::uint64_t to = std::min(offset + length, page_start + kPageSize) - page_start;
        std::memcpy(together.data() + done, page + from, static_cast<std::size_t>(to - from));
        done += to - from;
    }
    this->joined.push_back(std::move(together));
    return this->joined.back().data();
}

} // namespace the_seed::internal
