#pragma once

#include <libecs-cpp/ecs.hpp>

class sdl_input : public ecs::System
{
  public:
    sdl_input(); 
    sdl_input(Json::Value);
    Json::Value save();
    uint64_t data = 0;
    void Update(uint32_t dt);
};
