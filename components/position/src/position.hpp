#pragma once

#include <libecs-cpp/ecs.hpp>

class position : public ecs::Component
{
  public:
    position(); 
    position(Json::Value);
    Json::Value save();

    int x = 0, y = 0, z = 0;
};
