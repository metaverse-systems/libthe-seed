#include <cell.hpp>

cell::cell() 
{ 
    this->Type = "cell";
}

cell::cell(Json::Value config)
{
    this->Type = "cell";
    this->alive = config["alive"].asBool();
    this->x = config["x"].asUInt();
    this->y = config["y"].asUInt();
}

Json::Value cell::save()
{
    Json::Value config;
    config["alive"] = this->alive;
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
            return std::make_shared<cell>();
        }

        Json::Value *config = (Json::Value *)p;
        return std::make_shared<cell>(*config);
    }
}
