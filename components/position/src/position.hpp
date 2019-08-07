#pragma once

#include <libecs-cpp/ecs.hpp>

class position : public ecs::Component
{
  public:
    position(); 
    position(Json::Value);
    Json::Value save();

    uint32_t x = 0, y = 0, z = 0;
};
