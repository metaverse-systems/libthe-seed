#pragma once

#include <libecs-cpp/ecs.hpp>
#include <map>

class sdl : public ecs::System
{
  public:
    sdl(); 
    sdl(Json::Value);
    Json::Value save();
    void Update(uint32_t dt);
    void Init();
    uint16_t width, height;
  private:
    void *window = nullptr;
    void *renderer = nullptr;
    void *screen_surface = nullptr;
    std::map<std::string, void *> image_cache;
    std::map<std::string, void *> tex_cache;
    uint16_t columns = 80, rows = 25;
    float scale = 1;
    std::string title = "SDL Window";
    bool running = true;
    Json::Value images;
};
