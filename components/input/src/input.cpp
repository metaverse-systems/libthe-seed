#include <input.hpp>

input::input() 
{ 
    this->Type = "input";
}

input::input(Json::Value config)
{
    this->Type = "input";
    this->action = config["action"].asString();
    this->content = config["content"];
}

Json::Value input::save()
{
    Json::Value config;
    config["action"] = this->action;
    config["content"] = this->content;
    return config;
}

extern "C"
{
    std::shared_ptr<ecs::Component> create_component(void *p)
    {
        if(p == nullptr)
        {
            return std::make_shared<input>();
        }

        Json::Value *config = (Json::Value *)p;
        return std::make_shared<input>(*config);
    }
}
