#pragma once

// Generates resource paks for tests, byte for byte as the-seed's
// ResourcePak.build() (the-seed/src/ResourcePak.ts) writes them, and damages
// them in each way the reader has to report.
//
//   description line  JSON.stringify output: no spaces, keys in the order
//                     name, headerSize, resources; resource keys in the order
//                     name, size, attributes (only when present)
//   headerSize        string of 10 digits, zero padded, equal to the length
//                     of the line in UTF-16 code units plus one (the writer's
//                     count; for a non-ASCII name that is not the byte
//                     offset, which makes such a pak damaged)
//   data              the resources back to back
//
// A pak is built in memory as a std::vector<std::uint8_t> and written with
// WritePakFile. WriteLargePak writes a big pak in pieces instead.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace seedtest
{

struct PakResourceSpec
{
    std::string name;
    std::vector<std::uint8_t> bytes;
    // The attributes value as compact JSON text, as JSON.stringify writes it.
    // Empty means the resource has no "attributes" key.
    std::string attributes;
};

struct PakSpec
{
    std::string name;
    std::vector<PakResourceSpec> resources;
};

namespace pakdetail
{

// JSON.stringify of a string: short escapes for the usual characters, \u00xx
// (lowercase hex) for the other control characters, everything else raw.
inline std::string JsonString(const std::string &text)
{
    static const char *hex = "0123456789abcdef";
    std::string out = "\"";
    for(const char raw : text)
    {
        const unsigned char c = static_cast<unsigned char>(raw);
        switch(c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if(c < 0x20)
            {
                out += "\\u00";
                out += hex[c >> 4];
                out += hex[c & 0x0f];
            }
            else
            {
                out += raw;
            }
        }
    }
    out += '"';
    return out;
}

// The length JavaScript reports for UTF-8 text: one unit per code point, two
// for a code point outside the basic plane (a four byte sequence).
inline std::size_t Utf16Length(const std::string &text)
{
    std::size_t units = 0;
    for(const char raw : text)
    {
        const unsigned char c = static_cast<unsigned char>(raw);
        if((c & 0xC0) == 0x80)
        {
            continue; // continuation byte
        }
        units += (c >= 0xF0) ? 2 : 1;
    }
    return units;
}

inline std::string Padded(std::uint64_t value)
{
    std::string digits = std::to_string(value);
    if(digits.size() < 10)
    {
        digits.insert(0, 10 - digits.size(), '0');
    }
    return digits;
}

} // namespace pakdetail

// One resource of a description: its name, declared size and attributes text.
struct PakEntryText
{
    std::string name;
    std::uint64_t size;
    std::string attributes;
};

// The description line (without the newline), computed as the writer does:
// first with a placeholder headerSize, then with the real one.
inline std::string PakDescriptionLine(const std::string &pak_name, const std::vector<PakEntryText> &entries)
{
    std::string resources = "[";
    for(std::size_t i = 0; i < entries.size(); ++i)
    {
        if(i != 0)
        {
            resources += ',';
        }
        resources += "{\"name\":" + pakdetail::JsonString(entries[i].name) +
                     ",\"size\":" + std::to_string(entries[i].size);
        if(!entries[i].attributes.empty())
        {
            resources += ",\"attributes\":" + entries[i].attributes;
        }
        resources += '}';
    }
    resources += ']';

    const std::string head = "{\"name\":" + pakdetail::JsonString(pak_name) + ",\"headerSize\":\"";
    const std::string tail = "\",\"resources\":" + resources + "}";
    const std::string placeholder = pakdetail::Padded(0);
    const std::size_t length = pakdetail::Utf16Length(head + placeholder + tail);
    return head + pakdetail::Padded(length + 1) + tail;
}

// The exact bytes of the pak the writer would produce for `spec`.
inline std::vector<std::uint8_t> PakBytes(const PakSpec &spec)
{
    std::vector<PakEntryText> entries;
    for(const PakResourceSpec &resource : spec.resources)
    {
        entries.push_back({resource.name, resource.bytes.size(), resource.attributes});
    }
    const std::string line = PakDescriptionLine(spec.name, entries);

    std::vector<std::uint8_t> bytes(line.begin(), line.end());
    bytes.push_back('\n');
    for(const PakResourceSpec &resource : spec.resources)
    {
        bytes.insert(bytes.end(), resource.bytes.begin(), resource.bytes.end());
    }
    return bytes;
}

// Writes bytes to `path`, replacing any file there.
inline void WriteBytes(const std::filesystem::path &path, const std::vector<std::uint8_t> &bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if(!out)
    {
        throw std::runtime_error("cannot write " + path.string());
    }
}

// Writes `dir/file_name` holding the pak for `spec`. Returns its path.
inline std::filesystem::path WritePakFile(const std::filesystem::path &dir, const std::string &file_name,
                                          const PakSpec &spec)
{
    const std::filesystem::path path = dir / file_name;
    WriteBytes(path, PakBytes(spec));
    return path;
}

// ---- damage ---------------------------------------------------------------

namespace pakdetail
{

inline std::size_t LineEnd(const std::vector<std::uint8_t> &bytes)
{
    for(std::size_t i = 0; i < bytes.size(); ++i)
    {
        if(bytes[i] == '\n')
        {
            return i;
        }
    }
    throw std::runtime_error("pak bytes hold no newline");
}

// Replaces the description line of `bytes` with `line`, keeping the newline
// and the data after it.
inline std::vector<std::uint8_t> LineReplace(const std::vector<std::uint8_t> &bytes, const std::string &line)
{
    const std::size_t end = LineEnd(bytes);
    std::vector<std::uint8_t> out(line.begin(), line.end());
    out.insert(out.end(), bytes.begin() + static_cast<std::ptrdiff_t>(end), bytes.end());
    return out;
}

inline std::string LineGet(const std::vector<std::uint8_t> &bytes)
{
    const std::size_t end = LineEnd(bytes);
    return std::string(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(end));
}

inline const std::string headerSizeKey = "\"headerSize\":";

// Sets the headerSize string of `line`, if it has the writer's 10 digit
// form, to the byte length of the line plus one.
inline std::string HeaderSizeFix(std::string line)
{
    const std::size_t key = line.find(headerSizeKey);
    if(key == std::string::npos)
    {
        return line;
    }
    const std::size_t value = key + headerSizeKey.size();
    if(line.size() < value + 12 || line[value] != '"' || line[value + 11] != '"')
    {
        return line;
    }
    line.replace(value + 1, 10, Padded(line.size() + 1));
    return line;
}

} // namespace pakdetail

// The pak without its last `count` bytes.
inline std::vector<std::uint8_t> PakTruncate(std::vector<std::uint8_t> bytes, std::size_t count)
{
    bytes.resize(count > bytes.size() ? 0 : bytes.size() - count);
    return bytes;
}

// The pak followed by `count` extra bytes.
inline std::vector<std::uint8_t> PakAppend(std::vector<std::uint8_t> bytes, std::size_t count)
{
    bytes.insert(bytes.end(), count, static_cast<std::uint8_t>(0x5A));
    return bytes;
}

// Replaces the description line with `text` (no newline in it). With
// `recompute`, every run of ten '@' characters in `text` becomes the
// headerSize that matches the new line (byte length plus one); without it
// `text` is used as it is.
inline std::vector<std::uint8_t> PakDescriptionReplace(const std::vector<std::uint8_t> &bytes,
                                                       std::string text, bool recompute)
{
    if(recompute)
    {
        const std::string marker(10, '@');
        const std::string size = pakdetail::Padded(text.size() + 1);
        for(std::size_t at = text.find(marker); at != std::string::npos; at = text.find(marker, at + 10))
        {
            text.replace(at, 10, size);
        }
    }
    return pakdetail::LineReplace(bytes, text);
}

// Replaces the value of "headerSize" (the quoted digit string the writer
// wrote) with `raw_json`, for example `123`, `"12ab"` or `"123456789012345678901"`.
inline std::vector<std::uint8_t> PakHeaderSizeSet(const std::vector<std::uint8_t> &bytes,
                                                  const std::string &raw_json)
{
    std::string line = pakdetail::LineGet(bytes);
    const std::size_t key = line.find(pakdetail::headerSizeKey);
    if(key == std::string::npos)
    {
        throw std::runtime_error("pak description has no headerSize");
    }
    const std::size_t value = key + pakdetail::headerSizeKey.size();
    const std::size_t end = line.find(',', value);
    line.replace(value, end - value, raw_json);
    return pakdetail::LineReplace(bytes, line);
}

// Sets the declared size of the resource at `index` and recomputes the
// headerSize, so only the size is wrong.
inline std::vector<std::uint8_t> PakSizeSet(const std::vector<std::uint8_t> &bytes, std::size_t index,
                                            std::uint64_t size)
{
    std::string line = pakdetail::LineGet(bytes);
    const std::string key = "\"size\":";
    std::size_t at = line.find("\"resources\"");
    for(std::size_t i = 0; i <= index; ++i)
    {
        at = line.find(key, at);
        if(at == std::string::npos)
        {
            throw std::runtime_error("pak description has no such resource");
        }
        if(i != index)
        {
            at += key.size();
        }
    }
    const std::size_t value = at + key.size();
    std::size_t end = value;
    while(end < line.size() && line[end] >= '0' && line[end] <= '9')
    {
        ++end;
    }
    line.replace(value, end - value, std::to_string(size));
    return pakdetail::LineReplace(bytes, pakdetail::HeaderSizeFix(line));
}

// Repeats the description entry at `index` right after itself (same name),
// recomputing headerSize. The data is not repeated.
inline std::vector<std::uint8_t> PakEntryDuplicate(const std::vector<std::uint8_t> &bytes, std::size_t index)
{
    std::string line = pakdetail::LineGet(bytes);
    std::size_t at = line.find("\"resources\":[");
    if(at == std::string::npos)
    {
        throw std::runtime_error("pak description has no resources");
    }
    at += std::string("\"resources\":[").size();
    for(std::size_t i = 0;; ++i)
    {
        // An entry is one object; the writer's entries hold no nested braces
        // unless attributes do, which the tests that duplicate never use.
        const std::size_t end = line.find('}', at) + 1;
        if(i == index)
        {
            const std::string entry = line.substr(at, end - at);
            line.insert(end, "," + entry);
            break;
        }
        at = end + 1;
    }
    return pakdetail::LineReplace(bytes, pakdetail::HeaderSizeFix(line));
}

// Adds a key holding arrays nested `depth` levels deep to the description,
// recomputing headerSize.
inline std::vector<std::uint8_t> PakDescriptionNest(const std::vector<std::uint8_t> &bytes, std::size_t depth)
{
    std::string line = pakdetail::LineGet(bytes);
    const std::string nest = std::string(depth, '[') + std::string(depth, ']');
    line.insert(line.size() - 1, ",\"extra\":" + nest);
    return pakdetail::LineReplace(bytes, pakdetail::HeaderSizeFix(line));
}

// The pak with its first newline taken out. The data must hold no newline.
inline std::vector<std::uint8_t> PakNewlineRemove(std::vector<std::uint8_t> bytes)
{
    bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(pakdetail::LineEnd(bytes)));
    return bytes;
}

// ---- large pak ----------------------------------------------------------------

// The large pak of the cost tests: one resource of 1 KiB followed by 1,024
// resources of 256 KiB (256 MiB of data, 1,025 resources).
constexpr std::size_t LargePakResourceCount = 1025;

inline std::string LargePakResourceName(std::size_t index)
{
    if(index == 0)
    {
        return "small";
    }
    std::string number = std::to_string(index);
    number.insert(0, 4 - std::min<std::size_t>(4, number.size()), '0');
    return "big_" + number;
}

inline std::uint64_t LargePakResourceSize(std::size_t index)
{
    return index == 0 ? 1024u : 256u * 1024u;
}

// Byte `position` of resource `index`; tests check what they read against it.
inline std::uint8_t LargePakByte(std::size_t index, std::uint64_t position)
{
    return static_cast<std::uint8_t>((index * 31u + position * 7u + (position >> 8)) & 0xFFu);
}

// Writes `dir/<name>.pak` with the layout above, without holding it in
// memory. Returns its path.
inline std::filesystem::path WriteLargePak(const std::filesystem::path &dir, const std::string &name)
{
    std::vector<PakEntryText> entries;
    for(std::size_t i = 0; i < LargePakResourceCount; ++i)
    {
        entries.push_back({LargePakResourceName(i), LargePakResourceSize(i), ""});
    }
    const std::string line = PakDescriptionLine("seed/" + name, entries);

    std::filesystem::create_directories(dir);
    const std::filesystem::path path = dir / (name + ".pak");
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << line << '\n';
    std::vector<char> chunk(1024 * 1024);
    for(std::size_t i = 0; i < LargePakResourceCount; ++i)
    {
        const std::uint64_t size = LargePakResourceSize(i);
        for(std::uint64_t done = 0; done < size;)
        {
            const std::uint64_t count = std::min<std::uint64_t>(chunk.size(), size - done);
            for(std::uint64_t k = 0; k < count; ++k)
            {
                chunk[k] = static_cast<char>(LargePakByte(i, done + k));
            }
            out.write(chunk.data(), static_cast<std::streamsize>(count));
            done += count;
        }
    }
    out.close();
    if(!out)
    {
        throw std::runtime_error("cannot write " + path.string());
    }
    return path;
}

} // namespace seedtest
