#include <SKELETON.hpp>

SKELETON::SKELETON() 
{ 
    this->Type = "SKELETON";
}

SKELETON::SKELETON(uint64_t value)
{
    this->Type = "SKELETON";
    this->data = value;
}

extern "C"
{
    ecs::Component *create_component(void *p)
    {
        if(p == nullptr) return new SKELETON();

        uint64_t value = (uint64_t)(uint64_t *)p;
        return new SKELETON(value);
    }
}
