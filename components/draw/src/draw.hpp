#pragma once

#include <libecs-cpp/ecs.hpp>

class draw : public ecs::Component
{
  public:
    draw(); 
    draw(Json::Value);
    Json::Value Export();
    uint32_t width, height;
    uint8_t r, g, b, a;
};
