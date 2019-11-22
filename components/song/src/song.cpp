#include <song.hpp>

song::song() 
{ 
    this->Type = "song";
}

song::song(Json::Value config)
{
    this->Type = "song";
    this->resource_pak = config["resource_pak"].asString();
    this->name = config["name"].asString();
    this->status = config["status"].asString();
}

Json::Value song::save()
{
    Json::Value config;
    return config;
}

extern "C"
{
    ecs::Component *create_component(void *p)
    {
        if(p == nullptr)
        {
            return new song();
        }

        Json::Value *config = (Json::Value *)p;
        return new song(*config);
    }
}
