#include <libthe-seed/ResourcePak.hpp>
#include <fstream>

ResourcePak::ResourcePak(std::string resource_pak)
{
    this->name = resource_pak;

    std::streampos size;
    std::ifstream file(this->name + ".pak", std::ios::binary);
    if(file.good())
    {
        // get its size:
        file.seekg(0, std::ios::end);
        size = file.tellg();
        file.seekg(0, std::ios::beg);

        this->raw = new char[size];
        file.read(this->raw, size);
        file.close();
        this->load_file();
        return;
    }

    /* Load from directory */
}

void ResourcePak::load_file()
{
    Json::Value header;

    Json::Reader reader;
    bool parsingSuccessful = reader.parse(this->raw, header);  
    if(!parsingSuccessful)
    {
        std::string err = "Couldn't parse header for " + this->name;
        throw std::runtime_error(err);
    }

    std::string header_size = header["header_size"].asString();
    this->header_size = std::stoul(header_size);
    uint64_t pointer = this->header_size;
    for(auto &r : header["resources"])
    {
        ecs::Resource temp;
        temp.ptr = (char *)(&this->raw[pointer]);
        temp.size = r["bytes"].asUInt();
        this->resource_map[r["name"].asString()] = temp;
        pointer += temp.size;
    }
}

ecs::Resource ResourcePak::get(std::string name)
{
    return this->resource_map[name];
}
