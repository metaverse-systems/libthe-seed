#include <libthe-seed/ResourcePak.hpp>
#include <fstream>
#include <stdexcept>

ResourcePak::ResourcePak(const std::string &filename): filename(filename)
{
    std::ifstream file(this->filename, std::ios::binary);
    if(!file.good())
    {
        throw std::runtime_error("Couldn't open resource pak: " + filename);
    }

    // get its size:
    file.seekg(0, std::ios::end);
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);

    this->raw.resize(size);
    file.read(this->raw.data(), size);
    file.close();

    // copy from this->raw.data() until the first new line into rawHeader
    std::string rawHeader;
    for(auto &c : this->raw)
    {
        if(c == '\n')
        {
            break;
        }
        rawHeader += c;
    }

    this->header = nlohmann::json::parse(rawHeader);

    this->header_size = std::stoul(this->header["headerSize"].get<std::string>());

    uint64_t pointer = this->header_size;
    for(auto &resource : this->header["resources"])
    {
        auto rname = resource["name"].get<std::string>();
        auto rsize = resource["size"].get<uint64_t>();
        this->offset_map[rname] = {pointer, rsize};
        pointer += rsize;
    }
}

void ResourcePak::Load(ecs::Container *container, const std::string &name)
{
    container->ResourceAdd(name, this->Load(name));
}

ecs::Resource ResourcePak::Load(const std::string &name)
{
    auto it = this->offset_map.find(name);
    if(it == this->offset_map.end())
    {
        throw std::runtime_error("Resource " + name + " not found");
    }

    auto [pointer, size] = it->second;
    if(pointer + size > static_cast<uint64_t>(this->raw.size()))
    {
        throw std::runtime_error("Resource '" + name + "' exceeds pak data bounds");
    }

    ecs::Resource temp;
    temp.Data.assign(this->raw.begin() + pointer, this->raw.begin() + pointer + size);
    return temp;
}

void ResourcePak::LoadAll(ecs::Container *container)
{
    for(auto &resource : this->header["resources"])
    {
        this->Load(container, resource["name"].get<std::string>());
    }
}
