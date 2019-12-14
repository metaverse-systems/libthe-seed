#include <gui.hpp>

gui::gui() 
{ 
    this->Handle = "gui";
}

gui::gui(Json::Value config)
{
    this->Handle = "gui";
    this->visible = config["visible"].asBool();
}

Json::Value gui::Export()
{
    Json::Value config;
    config["visible"] = this->visible ? "true" : "false";
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
