#include "JSONLoader.hpp"
#include "ComponentLoader.hpp"

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
            auto e = this->container->Entity();
            e->Handle = entity["Handle"].asString();

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

    void Parse(ecs::Container *container, std::string data)
    {
        auto loader = Loader(container, data);
        loader.Parse();
    }

    void Parse(ecs::Container *container, std::string data, Json::Value config)
    {
        auto loader = Loader(container, data);
        loader.Parse();
    }
}
