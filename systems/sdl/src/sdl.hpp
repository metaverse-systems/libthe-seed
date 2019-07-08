#pragma once

#include <libecs-cpp/ecs.hpp>
#include <SDL.h>
#include <map>

class sdl : public ecs::System
{
  public:
    sdl(); 
    sdl(Json::Value);
    Json::Value save();
    void Update(uint32_t dt);
    void Init();
  private:
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    SDL_Surface *screen_surface = nullptr;
    std::map<std::string, SDL_Surface *> image_cache;
    std::map<std::string, SDL_Texture *> tex_cache;
    uint16_t width = 640, height = 480, columns = 80, rows = 25;
    float scale = 1;
    std::string title = "SDL Window";
    bool running = true;
    Json::Value images;
};
