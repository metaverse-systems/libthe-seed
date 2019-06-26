#pragma once

#include <libecs-cpp/ecs.hpp>

class shape : public ecs::Component
{
  public:
    shape(); 
    shape(Json::Value);
    Json::Value save();
    uint16_t x, y;
    uint16_t width, height;
    uint8_t r, g, b, a;
};
