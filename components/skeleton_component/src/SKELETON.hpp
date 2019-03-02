#pragma once

#include <libecs-cpp/ecs.hpp>

class SKELETON : public ecs::Component
{
  public:
    SKELETON(); 
    SKELETON(uint64_t);
    uint64_t data = 0;
};
