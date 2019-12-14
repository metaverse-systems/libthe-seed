#include <draw.hpp>

draw::draw() 
{ 
    this->Type = "draw";
}

draw::draw(Json::Value config)
{
    this->Type = "draw";
    this->width = std::stoul(config["width"].asString());
    this->height = std::stoul(config["height"].asString());
    this->r = std::stoul(config["r"].asString());
    this->g = std::stoul(config["g"].asString());
    this->b = std::stoul(config["b"].asString());
    this->a = std::stoul(config["a"].asString());
}

Json::Value draw::Export()
{
    Json::Value config;
    config["height"] = this->height;
    config["width"] = this->width;
    config["r"] = this->r;
    config["g"] = this->g;
    config["b"] = this->b;
    config["a"] = this->a;
    return config;
}

extern "C"
{
    ecs::Component *create_component(void *p)
    {
        if(p == nullptr)
        {
            return new draw();
        }

        Json::Value *config = (Json::Value *)p;
        return new draw(*config);
    }
}
