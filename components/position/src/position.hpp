#pragma once

#include <libecs-cpp/ecs.hpp>

class position : public ecs::Component
{
  public:
    position(); 
    position(Json::Value);
    Json::Value Export();

    float x = 0.0, y = 0.0, z = 0.0;
};
