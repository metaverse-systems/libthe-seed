#pragma once

#include <libecs-cpp/ecs.hpp>

class sdl_video : public ecs::System
{
  public:
    sdl_video(); 
    sdl_video(Json::Value);
    Json::Value save();
    void Update();
    void Init();
    uint16_t width, height;
    bool fullscreen = true;
  private:
    void *window = nullptr;
    void *renderer = nullptr;
    void *screen_surface = nullptr;
    std::string title = "SDL Window";
};
