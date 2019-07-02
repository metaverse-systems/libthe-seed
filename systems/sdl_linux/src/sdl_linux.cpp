#include <sdl_linux.hpp>
#include <SDL_image.h>
#include <iostream>
#include "../../components/position/src/position.hpp"
#include "../../components/texture/src/texture.hpp"
#include "../../components/background/src/background.hpp"
#include "../../components/shape/src/shape.hpp"

sdl_linux::sdl_linux() 
{ 
    this->Handle = "sdl_linux";
}

sdl_linux::sdl_linux(Json::Value config)
{
    this->Handle = "sdl_linux";
    this->height = config["height"].asUInt();
    this->width = config["width"].asUInt();
    if(config["scale"].asString().size()) this->scale = config["scale"].asFloat();
    this->title = config["title"].asString();
    this->columns = config["columns"].asUInt();
    this->rows = config["rows"].asUInt();

    if(SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        throw std::string("Couldn't initialize SDL video.");
    }

    this->window = SDL_CreateWindow(this->title.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 
                                    this->width, this->height, SDL_WINDOW_SHOWN);
    if(this->window == nullptr)
    {
        std::string message = "Window could not be created! SDL_Error: " + std::string(SDL_GetError());
        throw message;
    }

    this->renderer = SDL_CreateRenderer(this->window, -1, SDL_RENDERER_ACCELERATED);

    this->screen_surface = SDL_GetWindowSurface(this->window);

    for(uint16_t counter = 0; counter < config["images"].size(); counter++)
    {
        std::string filename = config["images"][counter].asString();
        std::cout << "Loading " << filename << std::endl;
        this->image_cache[filename] = IMG_Load(filename.c_str());
        this->tex_cache[filename] = SDL_CreateTextureFromSurface(this->renderer, this->image_cache[filename]);
    }

    this->ComponentRequest("texture");
}

Json::Value sdl_linux::save()
{
    throw std::string("save() not implemented");
    Json::Value config;
    return config;
}

void sdl_linux::Update(uint32_t dt)
{
    if(running)
    {
        SDL_Event event;
        while(SDL_PollEvent(&event))
        {
            if(event.type == SDL_QUIT) this->running = false;
        }

        SDL_SetRenderDrawColor(this->renderer, 0, 0, 0, 0xFF );
        SDL_RenderClear(this->renderer);

        ecs::ComponentMap Components = this->ComponentsGet();

        for(auto &c : Components["texture"])
        {
            auto t = (texture *)c.second;
            auto p = (position *)this->Container->Entity(t->EntityHandle)->ComponentGet("position");

            SDL_Rect src = { t->col * t->width, t->row * t->height, t->width, t->height };
            SDL_Rect dest = { p->x * (t->width * this->scale), p->y * (t->height * this->scale), t->width * this->scale, t->height * this->scale };
            SDL_SetTextureColorMod(this->tex_cache[t->tex_filename], t->r, t->g, t->b);
            SDL_RenderCopy(this->renderer, this->tex_cache[t->tex_filename], &src, &dest);
        }

        SDL_SetRenderDrawBlendMode(this->renderer, SDL_BLENDMODE_BLEND);
        SDL_RenderPresent(this->renderer);
    }
    else
    {
        SDL_DestroyWindow(this->window);
        SDL_Quit();
        this->Container->ManagerGet()->Shutdown();
    }
}

extern "C"
{
    ecs::System *create_system(void *p)
    {
        if(p == nullptr) return new sdl_linux();

        Json::Value *config = (Json::Value *)p;
        return new sdl_linux(*config);
    }

    void *get_system()
    {
        return (void *)create_system;
    }
}
