#pragma once

#include <libecs-cpp/ecs.hpp>

class SKELETON : public ecs::System
{
  public:
    SKELETON(); 
    SKELETON(uint64_t);
    uint64_t data = 0;
    void Update(uint32_t dt);
};
