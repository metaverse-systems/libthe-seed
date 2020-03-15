#pragma once

#include <libecs-cpp/ecs.hpp>

class SKELETON : public ecs::System
{
  public:
    SKELETON(); 
    SKELETON(Json::Value);
    Json::Value Export();
    uint64_t data = 0;
    void Update();
};
