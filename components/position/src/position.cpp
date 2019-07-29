#include <position.hpp>

position::position() 
{ 
    this->Type = "position";
}

position::position(Json::Value config)
{
    this->Type = "position";
    this->x = config["x"].asFloat();
    this->y = config["y"].asFloat();
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
    std::shared_ptr<ecs::Component> create_component(void *p)
    {
        if(p == nullptr)
        {
            return std::make_shared<position>();
        }

        Json::Value *config = (Json::Value *)p;
        return std::make_shared<position>(*config);
    }
}
