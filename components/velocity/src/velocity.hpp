#pragma once

#include <libecs-cpp/ecs.hpp>

class velocity : public ecs::Component
{
  public:
    velocity(); 
    velocity(Json::Value);
    Json::Value save();

    float x = 0.0, y = 0.0, z = 0.0;
};
