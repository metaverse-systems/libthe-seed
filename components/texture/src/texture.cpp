#include <texture.hpp>

texture::texture() 
{ 
    this->Type = "texture";
}

texture::texture(Json::Value config)
{
    this->Type = "texture";
    this->tex_filename = config["tex_filename"].asString();
    this->width = config["width"].asInt();
    this->height = config["height"].asInt();
    this->col = config["col"].asInt();
    this->row = config["row"].asInt();
    this->r = config["r"].asUInt();
    this->g = config["g"].asUInt();
    this->b = config["b"].asUInt();
    this->a = config["a"].asUInt();
}

texture::~texture()
{
}

Json::Value texture::Export()
{
    Json::Value config;
    return config;
}

extern "C"
{
    std::shared_ptr<ecs::Component> create_component(void *p)
    {
        if(p == nullptr)
        {
            return std::make_shared<texture>();
        }

        Json::Value *config = (Json::Value *)p;
        return std::make_shared<texture>(*config);
    }
}
