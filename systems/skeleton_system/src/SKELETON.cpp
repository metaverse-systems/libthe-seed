#include <SKELETON.hpp>

SKELETON::SKELETON() 
{ 
    this->Handle = "SKELETON";
}

SKELETON::SKELETON(Json::Value config)
{
    this->Handle = "SKELETON";
    this->data = config["data"].asUInt64();
}

Json::Value SKELETON::Export()
{
    Json::Value config;
    config["data"] = (Json::UInt64)this->data;
    return config;
}

void SKELETON::Update()
{
    auto dt = this->DeltaTimeGet();
    // It's been dt milliseconds since the last Update()
    // Do some work
}

extern "C"
{
    ecs::System *create_system(void *p)
    {
        if(p == nullptr) return new SKELETON();

        Json::Value *config = (Json::Value *)p;
        return new SKELETON(*config);
    }
}
