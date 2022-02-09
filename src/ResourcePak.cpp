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

    this->raw = new char[size];
    file.read(this->raw, size);
    file.close();

    Json::Reader reader;
    if(!reader.parse(this->raw, this->header))
    {
        throw std::runtime_error("Couldn't parse header for " + this->filename);
    }

    this->header_size = std::stoul(this->header["header_size"].asString());
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
        if(resource["name"].asString() != name)
        {
            pointer += resource["bytes"].asUInt();
            continue;
        }

        ecs::Resource temp;
        temp.ptr = (char *)(&this->raw[pointer]);
        temp.size = resource["bytes"].asUInt();
        return temp;
    }

    throw std::runtime_error("Resource " + name + " not found");
}

void ResourcePak::LoadAll(ecs::Container *container)
{
    for(auto &resource : this->header["resources"])
    {
        this->Load(container, resource["name"].asString());
    }
}
