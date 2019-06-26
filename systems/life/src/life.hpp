#pragma once

#include <libecs-cpp/ecs.hpp>

class life : public ecs::System
{
  public:
    life(); 
    life(Json::Value);
    Json::Value save();
    void Update(uint32_t dt);
  private:
    uint32_t ms = 0;
};
