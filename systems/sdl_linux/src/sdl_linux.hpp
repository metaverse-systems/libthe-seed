#pragma once

#include <libecs-cpp/ecs.hpp>
#include <SDL.h>
#include <map>

class sdl_linux : public ecs::System
{
  public:
    sdl_linux(); 
    sdl_linux(Json::Value);
    Json::Value save();
    void Update(uint32_t dt);
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
};
