#include <position.hpp>

position::position() 
{ 
    this->Type = "position";
}

position::position(Json::Value config)
{
    this->Type = "position";
    this->x = config["x"].asUInt();
    this->y = config["y"].asUInt();
}

Json::Value position::save()
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
        if(p == nullptr)
        {
            return new position();
        }

        Json::Value *config = (Json::Value *)p;
        return new position(*config);
    }
}
