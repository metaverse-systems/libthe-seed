#include <velocity.hpp>

velocity::velocity() 
{ 
    this->Type = "velocity";
}

velocity::velocity(Json::Value config)
{
    this->Type = "velocity";
    this->x = config["x"].asFloat();
    this->y = config["y"].asFloat();
}

Json::Value velocity::save()
{
    Json::Value config;
    config["x"] = this->x;
    config["y"] = this->y;
    return config;
}

extern "C"
{
    ecs::Component *create_component(void *p)
    {
        if(p == nullptr) return new velocity();

        Json::Value *config = (Json::Value *)p;
        return new velocity(*config);
    }
}
