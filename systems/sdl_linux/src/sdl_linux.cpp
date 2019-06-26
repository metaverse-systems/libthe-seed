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
    this->ComponentRequest("background");
    this->ComponentRequest("shape");
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

        std::vector<std::string> RequestedComponents;
        RequestedComponents.push_back("texture");
        RequestedComponents.push_back("background");
        RequestedComponents.push_back("shape");
        std::map<std::string, std::map<std::string, ecs::Component *>> Components = this->Container->ComponentsGet(RequestedComponents);

/*
        for(auto &c : Components["background"])
        {
            auto b = (background *)c.second;
            if(b->tex == nullptr)
            {
                b->tex = SDL_CreateTextureFromSurface(this->renderer, b->image);
            }

            auto p = (position *)this->Container->Entity(b->EntityHandle)->ComponentGet("position");
            SDL_Rect dest_rect = { p->x, p->y, b->tex_rect.w, b->tex_rect.h };

            SDL_RenderCopy(this->renderer, b->tex, &b->tex_rect, &dest_rect);
        }
*/

        for(auto &c : Components["texture"])
        {
            auto t = (texture *)c.second;
            if(t->tex == nullptr) t->tex = this->tex_cache[t->tex_filename];
/*
            {
                SDL_Rect dest = { 0, 0, 16, 16 };
                uint16_t x = t->tex_index % 16;
                uint16_t y = (t->tex_index / 16);
std::cout << "Loading index " << t->tex_index << " at coordinates " << x << ", " << y << std::endl;
                SDL_Rect src = { x * 16, y * 16, 16, 16 };
//                SDL_RenderCopy(this->renderer, t->tex, &src, &dest);
            }
*/

//            auto p = (position *)this->Container->Entity(t->EntityHandle)->ComponentGet("position");
//            SDL_Rect dest_rect = { p->x, p->y, t->tex_rect.w, t->tex_rect.h };
//            SDL_RenderCopy(this->renderer, t->tex, &t->tex_rect, &dest_rect);

        }

        SDL_SetRenderDrawBlendMode(this->renderer, SDL_BLENDMODE_BLEND);
        for(auto &c : Components["shape"])
        {
            auto s = (shape *)c.second;
            SDL_Rect rect = { s->x, s->y, s->width, s->height };
            SDL_SetRenderDrawColor(this->renderer, s->r, s->g, s->b, s->a);
            SDL_RenderFillRect(this->renderer, &rect );
        }
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
