#include <libthe-seed/ResourcePak.hpp>
#include <fstream>

ResourcePak::ResourcePak(std::string filename): filename(filename)
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

    this->header = nlohmann::json::parse(this->raw.data());
    this->header_size = std::stoul(this->header["header_size"].get<std::string>());
}

void ResourcePak::Load(ecs::Container *container, std::string name)
{
    container->ResourceAdd(name, this->Load(name));
}

ecs::Resource ResourcePak::Load(std::string name)
{
    uint64_t pointer = this->header_size;
    for(auto &resource : this->header["resources"])
    {
        if(resource["name"].get<std::string>() != name)
        {
            pointer += resource["bytes"].get<uint64_t>();
            continue;
        }

        ecs::Resource temp;
        temp.ptr = (char *)(&this->raw[pointer]);
        temp.size = resource["bytes"].get<uint64_t>();
        return temp;
    }

    throw std::runtime_error("Resource " + name + " not found");
}

void ResourcePak::LoadAll(ecs::Container *container)
{
    for(auto &resource : this->header["resources"])
    {
        this->Load(container, resource["name"].get<std::string>());
    }
}
