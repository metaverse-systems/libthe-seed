#include <sdl_video.hpp>
#include <SDL.h>
#include "../../components/position/src/position.hpp"
#include "../../components/draw/src/draw.hpp"
#include <iostream>

void dump_rect(SDL_Rect &rect)
{
    std::cout << "x,y: " << rect.x << ", " << rect.y << ". width, height: " << rect.w << ", " << rect.h << std::endl;
}

sdl_video::sdl_video() 
{ 
    this->Handle = "sdl_video";
}

sdl_video::sdl_video(Json::Value config)
{
    this->Handle = "sdl_video";
    this->title = config["title"].asString();
    this->fullscreen = config["fullscreen"].asBool();
}

Json::Value sdl_video::Export()
{
    Json::Value config;
    config["title"] = this->title;
    config["fullscreen"] = this->fullscreen ? "true" : "false";
    return config;
}

void sdl_video::Init()
{
    this->ComponentRequest("draw");
    this->ComponentRequest("position");

    SDL_Init(0);
    if(SDL_InitSubSystem(SDL_INIT_VIDEO) < 0)
    {
        std::string message = "Couldn't initialize SDL video.";
        throw std::runtime_error(message);
    }

    SDL_DisplayMode current;
    if(SDL_GetCurrentDisplayMode(0, &current) != 0)
    {
        std::string message = "Couldn't get screen size.";
        throw std::runtime_error(message);
    }

    this->width = current.w;
    this->height = current.h;

    this->window = SDL_CreateWindow(this->title.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                    this->width, this->height, SDL_WINDOW_SHOWN);

    if(nullptr == this->window)
    {
        std::string message = "Window could not be created! SDL_Error: " + std::string(SDL_GetError());
        throw std::runtime_error(message);
    }

    if(!this->fullscreen)
    {
        std::string message = "Only fullscreen is supported right now";
        throw std::runtime_error(message);
    }

    SDL_SetWindowFullscreen((SDL_Window *)this->window, SDL_WINDOW_FULLSCREEN_DESKTOP);

    this->renderer = SDL_CreateRenderer((SDL_Window *)this->window, -1, SDL_RENDERER_ACCELERATED);
    if(nullptr == this->renderer)
    {
        std::string message = "Could not create accelerator! SDL_Error: " + std::string(SDL_GetError());
        throw std::runtime_error(message);
    }

    this->screen_surface = SDL_GetWindowSurface((SDL_Window *)this->window);
    if(nullptr == this->screen_surface)
    {
        std::string message = "Could not get window surface! SDL_Error: " + std::string(SDL_GetError());
        throw std::runtime_error(message);
    }
}

void sdl_video::Update()
{
    // Clear screen to black
    SDL_SetRenderDrawColor((SDL_Renderer *)this->renderer, 0, 0, 0, 0xFF );
    SDL_RenderClear((SDL_Renderer *)this->renderer);

    auto Components = this->ComponentsGet();

    for(auto &[entity, component_list] : Components["draw"])
    {
        while(auto dcomponent = component_list.Pop())
        {
            auto d = std::dynamic_pointer_cast<draw>(dcomponent);
            auto pcomponent = Components["position"][entity].Pop();
            auto pos = std::dynamic_pointer_cast<position>(pcomponent);
            if(pos == nullptr) continue;

            SDL_Rect rect = { (int)pos->x, (int)pos->y, (int)d->width, (int)d->height };
            SDL_SetRenderDrawColor((SDL_Renderer *)this->renderer, d->r, d->g, d->b, d->a);
            SDL_RenderFillRect((SDL_Renderer *)this->renderer, &rect);
        }
    }

    // Draw
    SDL_SetRenderDrawBlendMode((SDL_Renderer *)this->renderer, SDL_BLENDMODE_BLEND);
    SDL_RenderPresent((SDL_Renderer *)this->renderer);
}

extern "C"
{
    ecs::System *create_system(void *p)
    {
        if(p == nullptr) return new sdl_video();

        Json::Value *config = (Json::Value *)p;
        return new sdl_video(*config);
    }
}
