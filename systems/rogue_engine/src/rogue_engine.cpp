#include <rogue_engine.hpp>
#include "../../../components/input/src/input.hpp"
#include <iostream>

rogue_engine::rogue_engine() 
{ 
    this->Handle = "rogue_engine";
}

rogue_engine::rogue_engine(Json::Value config)
{
    this->Handle = "rogue_engine";
    this->data = config["data"].asUInt64();
}

Json::Value rogue_engine::save()
{
    Json::Value config;
    config["data"] = (Json::UInt64)this->data;
    return config;
}

void rogue_engine::Init()
{
    this->ComponentRequest("input");
}

void rogue_engine::Update(uint32_t dt)
{
    std::map<std::string, ecs::ComponentList> Components = this->Container->ComponentsGet();

    while(!Components["input"].empty())
    {
        auto c = Components["input"].back();
        Components["input"].pop_back();
        auto i = std::dynamic_pointer_cast<input>(c);

        std::cout << i->action << std::endl;

        if(i->action == "keyup")
        {
            std::cout << "Key: " << i->content << std::endl;
            if(i->content["key"] == " ");
        }

        if(i->action == "quit")
        {
            this->Container->ManagerGet()->Shutdown();
        }

        ecs::Entity *e = this->Container->Entity(i->EntityHandle);
        e->destroy();
        return;
    }

}

extern "C"
{
    ecs::System *create_system(void *p)
    {
        if(p == nullptr) return new rogue_engine();

        Json::Value *config = (Json::Value *)p;
        return new rogue_engine(*config);
    }
}
