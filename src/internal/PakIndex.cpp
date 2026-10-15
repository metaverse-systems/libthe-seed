#include "PakIndex.hpp"

#include "BoundedBytes.hpp"

#include <libecs-cpp/json.hpp>

#include <algorithm>
#include <atomic>
#include <cstring>

namespace seed::internal
{

namespace
{
std::atomic<std::uint64_t> parse_count{0};

constexpr std::uint64_t description_limit = 64ull * 1024 * 1024;
constexpr std::uint64_t chunk_size = 64 * 1024;
constexpr int nesting_limit = 64;
constexpr const char *format_name = "resource pak";

// The description line without its newline, and the offset just past that
// newline.
std::string DescriptionRead(PakFile &file, std::uint64_t file_size, std::uint64_t &end)
{
    const std::uint64_t scan = std::min(file_size, description_limit);
    std::string line;
    std::vector<char> chunk;

    std::uint64_t position = 0;
    while(position < scan)
    {
        const std::uint64_t count = std::min(chunk_size, scan - position);
        chunk.resize(static_cast<std::size_t>(count));
        try
        {
            file.ReadAt(position, chunk.data(), count);
        }
        catch(const PakShortRead &error)
        {
            throw PakDamaged(error.what());
        }

        const void *newline = std::memchr(chunk.data(), '\n', chunk.size());
        if(newline != nullptr)
        {
            const std::size_t length = static_cast<const char *>(newline) - chunk.data();
            line.append(chunk.data(), length);
            end = position + length + 1;
            return line;
        }
        line.append(chunk.data(), chunk.size());
        position += count;
    }

    if(file_size > description_limit)
    {
        throw PakDamaged("description line is longer than " + std::to_string(description_limit) + " bytes");
    }
    throw PakDamaged("no description line (no newline in the first " + std::to_string(file_size) + " bytes)");
}

nlohmann::json DescriptionParse(const std::string &line)
{
    bool too_deep = false;
    nlohmann::json::parser_callback_t callback = [&too_deep](int depth, nlohmann::json::parse_event_t event,
                                                             nlohmann::json &) {
        if((event == nlohmann::json::parse_event_t::object_start ||
            event == nlohmann::json::parse_event_t::array_start) &&
           depth + 1 > nesting_limit)
        {
            too_deep = true;
            return false;
        }
        return true;
    };

    nlohmann::json description;
    try
    {
        description = nlohmann::json::parse(line.begin(), line.end(), callback, true);
    }
    catch(const nlohmann::json::exception &error)
    {
        throw PakDamaged(std::string("description is not valid JSON: ") + error.what());
    }

    if(too_deep)
    {
        throw PakDamaged("description is nested deeper than " + std::to_string(nesting_limit) + " levels");
    }
    return description;
}

std::uint64_t HeaderSizeRead(const nlohmann::json &description)
{
    const auto found = description.find("headerSize");
    if(found == description.end())
    {
        throw PakDamaged("\"headerSize\" is missing");
    }
    if(!found->is_string())
    {
        throw PakDamaged("\"headerSize\" is not a string of decimal digits");
    }

    const std::string &text = found->get_ref<const std::string &>();
    if(text.empty() || text.size() > 20 ||
       !std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; }))
    {
        throw PakDamaged("\"headerSize\" is not a string of decimal digits");
    }

    std::uint64_t value = 0;
    for(char c : text)
    {
        const std::uint64_t digit = static_cast<std::uint64_t>(c - '0');
        if(value > (UINT64_MAX - digit) / 10)
        {
            throw PakDamaged("\"headerSize\" is out of range");
        }
        value = value * 10 + digit;
    }
    return value;
}

std::uint64_t SizeRead(const nlohmann::json &resource, const std::string &name)
{
    const auto found = resource.find("size");
    if(found == resource.end())
    {
        throw PakDamaged("resource \"" + name + "\" has no \"size\"");
    }
    if(!found->is_number_integer() || (!found->is_number_unsigned() && found->get<std::int64_t>() < 0))
    {
        throw PakDamaged("resource \"" + name + "\" \"size\" is not a non-negative integer");
    }
    return found->get<std::uint64_t>();
}
} // namespace

std::uint64_t PakDescriptionParseCount()
{
    return parse_count.load(std::memory_order_relaxed);
}

void ResetPakDescriptionParseCount()
{
    parse_count.store(0, std::memory_order_relaxed);
}

std::shared_ptr<const PakIndex> PakIndexRead(PakFile &file)
{
    auto index = std::make_shared<PakIndex>();
    index->stamp = file.Stamp();
    const std::uint64_t file_size = index->stamp.size;

    // The description line.
    std::uint64_t description_end = 0;
    const std::string line = DescriptionRead(file, file_size, description_end);

    // The description is JSON of limited depth.
    parse_count.fetch_add(1, std::memory_order_relaxed);
    const nlohmann::json description = DescriptionParse(line);

    // The top level is an object.
    if(!description.is_object())
    {
        throw PakDamaged("description is not a JSON object");
    }

    // headerSize is a digit string and matches the description's end.
    const std::uint64_t header_size = HeaderSizeRead(description);
    if(header_size != description_end)
    {
        throw PakDamaged("stated description length " + std::to_string(header_size) +
                         " does not match the description's end at byte " + std::to_string(description_end));
    }

    // resources is an array.
    const auto resources = description.find("resources");
    if(resources == description.end())
    {
        throw PakDamaged("\"resources\" is missing");
    }
    if(!resources->is_array())
    {
        throw PakDamaged("\"resources\" is not an array");
    }

    // Each resource: shape, unique name, sizes that do not overflow.
    index->entries.reserve(resources->size());
    std::uint64_t running = header_size;
    std::size_t position = 0;
    for(const nlohmann::json &resource : *resources)
    {
        if(!resource.is_object())
        {
            throw PakDamaged("resource " + std::to_string(position) + " is not an object");
        }
        const auto name = resource.find("name");
        if(name == resource.end())
        {
            throw PakDamaged("resource " + std::to_string(position) + " has no \"name\"");
        }
        if(!name->is_string())
        {
            throw PakDamaged("resource " + std::to_string(position) + " \"name\" is not a string");
        }

        PakEntry entry;
        entry.name = name->get<std::string>();
        entry.size = SizeRead(resource, entry.name);

        if(index->by_name.count(entry.name) != 0)
        {
            throw PakDamaged("resource \"" + entry.name + "\" appears more than once");
        }

        entry.offset = running;
        try
        {
            ToSize(entry.size, format_name, "size");
            running = CheckedAdd(running, entry.size, format_name, "end of resource");
        }
        catch(const std::runtime_error &)
        {
            throw PakDamaged("size of resource \"" + entry.name + "\" is out of range");
        }

        index->by_name.emplace(entry.name, index->entries.size());
        index->entries.push_back(std::move(entry));
        ++position;
    }

    // The sizes explain the whole file.
    if(running > file_size)
    {
        throw PakDamaged("the description claims " + std::to_string(running) + " bytes but the file holds " +
                         std::to_string(file_size));
    }
    if(running < file_size)
    {
        throw PakDamaged(std::to_string(file_size - running) + " bytes of unexplained data after the last resource");
    }

    index->description_size = header_size;
    return index;
}

} // namespace seed::internal
