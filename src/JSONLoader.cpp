#include <libthe-seed/JSONLoader.hpp>
#include <libthe-seed/ComponentLoader.hpp>
#include <fstream>

namespace JSONLoader
{
    Loader::Loader(ecs::Container *container, std::string data)
    {
        this->container = container;
        Json::Reader reader;
        if(!reader.parse(data.c_str(), this->scene))
        {
            std::string err = "Couldn't parse scene: " + data;
            throw std::runtime_error(err);
        }
    }

    void Loader::Parse()
    {
        for(auto entity : this->scene["entities"])
        {
            auto e = this->container->Entity(entity["Handle"].asString());

            for(auto type : entity["Components"].getMemberNames())
            {
                for(auto component : entity["Components"][type])
                {
                    e->Component(ComponentLoader::Create(type, &component));
                }
            }
        }
    }

    Loader::~Loader()
    {
    }

    void StringParse(ecs::Container *container, std::string data)
    {
        Loader(container, data).Parse();
    }

    void FileParse(ecs::Container *container, std::string filename)
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

        StringParse(container, data);
    }
}
