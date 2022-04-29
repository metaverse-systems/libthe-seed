#include <libthe-seed/JSONLoader.hpp>
#include <libthe-seed/ComponentLoader.hpp>
#include <fstream>

JSONLoader::JSONLoader(ecs::Container *container): container(container)
{
}

void JSONLoader::StringParse(std::string data)
{
    this->scene = nlohmann::json::parse(data);
    for(auto entity : this->scene["entities"])
    {
        auto e = this->container->Entity(entity["Handle"].get<std::string>());

        for(auto &[type, component] : entity["Components"].items())
        {
            e->Component(ComponentLoader::Create(type, &component));
        }
    }
}

void JSONLoader::FileParse(std::string filename)
{
    std::string data;
    std::ifstream file;
    std::streampos fsize, fstart = 0;

    file.open(filename);
    fstart = file.tellg();
    file.seekg(0, std::ios::end);
    fsize = file.tellg() - fstart;
    file.seekg(0, std::ios::beg);
    data.resize(fsize);
    file.read(&data[0], fsize);
    file.close();

    this->StringParse(data);
}

JSONLoader::~JSONLoader()
{
}
