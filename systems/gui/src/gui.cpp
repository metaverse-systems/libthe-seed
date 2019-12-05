#include <gui.hpp>

gui::gui() 
{ 
    this->Handle = "gui";
}

gui::gui(Json::Value config)
{
    this->Handle = "gui";
    this->data = config["data"].asUInt64();
}

Json::Value gui::save()
{
    Json::Value config;
    config["data"] = (Json::UInt64)this->data;
    return config;
}

void gui::Update()
{
    auto dt = this->DeltaTimeGet();
    // It's been dt milliseconds since the last Update()
    // Do some work
}

extern "C"
{
    ecs::System *create_system(void *p)
    {
        if(p == nullptr) return new gui();

        Json::Value *config = (Json::Value *)p;
        return new gui(*config);
    }
}
