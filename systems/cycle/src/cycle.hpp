#pragma once

#include <libecs-cpp/ecs.hpp>

class cycle : public ecs::System
{
  public:
    cycle(); 
    cycle(Json::Value);
    Json::Value save();
    void Update();
    void Init();
  private:
    bool paused = false;
    uint32_t ms = 0;
};
