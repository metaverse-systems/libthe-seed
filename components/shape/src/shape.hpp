#pragma once

#include <libecs-cpp/ecs.hpp>

class shape : public ecs::Component
{
  public:
    shape(); 
    shape(Json::Value);
    Json::Value save();
    uint32_t x, y;
    uint32_t width, height;
    uint8_t r, g, b, a;
};
