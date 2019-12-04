#include <imgui.hpp>

imgui::imgui() 
{ 
    this->Handle = "imgui";
}

imgui::imgui(Json::Value config)
{
    this->Handle = "imgui";
    this->data = config["data"].asUInt64();
}

Json::Value imgui::save()
{
    Json::Value config;
    config["data"] = (Json::UInt64)this->data;
    return config;
}

void imgui::Update()
{
    auto dt = this->DeltaTimeGet();
    // It's been dt milliseconds since the last Update()
    // Do some work
}

extern "C"
{
    ecs::System *create_system(void *p)
    {
        if(p == nullptr) return new imgui();

        Json::Value *config = (Json::Value *)p;
        return new imgui(*config);
    }
}
