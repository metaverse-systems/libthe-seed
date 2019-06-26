#pragma once

#include <libecs-cpp/ecs.hpp>

class cell : public ecs::Component
{
  public:
    cell(); 
    cell(Json::Value);
    Json::Value save();
    bool alive = false;
    uint16_t x, y;
};
