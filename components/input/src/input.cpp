#include <input.hpp>

input::input() 
{ 
    this->Type = "input";
}

input::input(Json::Value config)
{
    this->Type = "input";
    this->event = config["event"].asString();
    this->content = config["content"];
}

Json::Value input::Export()
{
    Json::Value config;
    config["event"] = this->event;
    config["content"] = this->content;
    return config;
}

extern "C"
{
    ecs::Component *create_component(void *p)
    {
        if(p == nullptr)
        {
            return new input();
        }

        Json::Value *config = (Json::Value *)p;
        return new input(*config);
    }
}
