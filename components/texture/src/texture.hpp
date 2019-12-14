#pragma once

#include <libecs-cpp/ecs.hpp>

class texture : public ecs::Component
{
  public:
    texture(); 
    texture(Json::Value);
    ~texture();
    Json::Value Export();
    std::string tex_filename;
    int width, height, col, row;
    uint8_t r, g, b, a;
};
