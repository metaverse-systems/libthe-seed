#include <draw.hpp>

draw::draw() 
{ 
    this->Type = "draw";
}

draw::draw(Json::Value config)
{
    this->Type = "draw";
    this->width = config["width"].asUInt();
    this->height = config["height"].asUInt();
    this->r = config["r"].asUInt();
    this->g = config["g"].asUInt();
    this->b = config["b"].asUInt();
    this->a = config["a"].asUInt();
}

Json::Value draw::save()
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
