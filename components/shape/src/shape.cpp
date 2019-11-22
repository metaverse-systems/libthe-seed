#include <shape.hpp>

shape::shape() 
{ 
    this->Type = "shape";
}

shape::shape(Json::Value config)
{
    this->Type = "shape";
    this->width = config["width"].asUInt();
    this->height = config["height"].asUInt();
    this->r = config["r"].asUInt();
    this->g = config["g"].asUInt();
    this->b = config["b"].asUInt();
    this->a = config["a"].asUInt();
    this->x = config["x"].asUInt();
    this->y = config["y"].asUInt();
}

Json::Value shape::save()
{
    Json::Value config;
    config["height"] = this->height;
    config["width"] = this->width;
    config["r"] = this->r;
    config["g"] = this->g;
    config["b"] = this->b;
    config["a"] = this->a;
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
            return new shape();
        }

        Json::Value *config = (Json::Value *)p;
        return new shape(*config);
    }
}
