#pragma once

#include <libecs-cpp/ecs.hpp>

class imgui : public ecs::System
{
  public:
    imgui(); 
    imgui(Json::Value);
    Json::Value save();
    uint64_t data = 0;
    void Update();
};
