#include <SKELETON.hpp>

SKELETON::SKELETON() 
{ 
    this->Type = "SKELETON";
}

SKELETON::SKELETON(Json::Value config)
{
    this->Type = "SKELETON";
    this->data = config["data"].asString();
}

Json::Value SKELETON::save()
{
    Json::Value config;
    config["data"] = this->data;
    return config;
}

extern "C"
{
    ecs::Component *create_component(void *p)
    {
        if(p == nullptr)
        {
            return new SKELETON();
        }

        Json::Value *config = (Json::Value *)p;
        return new SKELETON(*config);
    }
}
