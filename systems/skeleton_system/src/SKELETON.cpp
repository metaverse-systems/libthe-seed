#include <SKELETON.hpp>

SKELETON::SKELETON() 
{ 
    this->Handle = "SKELETON";
}

SKELETON::SKELETON(uint64_t value)
{
    this->Handle = "SKELETON";
    this->data = value;
}

void SKELETON::Update(uint32_t dt)
{
    // It's been dt milliseconds since the last Update()
    // Do some work
}

extern "C"
{
    ecs::System *create_system(void *p)
    {
        if(p == nullptr) return new SKELETON();

        uint64_t value = (uint64_t)(uint64_t *)p;
        return new SKELETON(value);
    }
}
