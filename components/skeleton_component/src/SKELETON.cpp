#include <SKELETON.hpp>

SKELETON::SKELETON() 
{ 
    this->Type = "SKELETON";
}

SKELETON::SKELETON(ConfigMap config)
{
    this->Type = "SKELETON";
    this->data = config["value"];
}

extern "C"
{
    ecs::Component *create_component(void *p)
    {
        if(p == nullptr) return new SKELETON();

        ConfigMap *cm = (ConfigMap *)p;
        return new SKELETON(*cm);
    }
}
