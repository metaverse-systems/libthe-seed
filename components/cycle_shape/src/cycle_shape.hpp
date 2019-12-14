#pragma once

#include <libecs-cpp/ecs.hpp>

class cycle_shape : public ecs::Component
{
  public:
    cycle_shape(); 
    cycle_shape(Json::Value);
    Json::Value Export();
    uint32_t width, height;
    uint8_t r, g, b, a;
};
