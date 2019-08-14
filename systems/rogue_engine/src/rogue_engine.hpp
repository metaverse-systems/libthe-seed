#pragma once

#include <libecs-cpp/ecs.hpp>

class rogue_engine : public ecs::System
{
  public:
    rogue_engine(); 
    rogue_engine(Json::Value);
    Json::Value save();
    uint64_t data = 0;
    void Init();
    void Update(uint32_t dt);
};
