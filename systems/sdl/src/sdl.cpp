#include <sdl.hpp>
#include <SDL_image.h>
#include "../../components/position/src/position.hpp"
#include "../../components/texture/src/texture.hpp"
#include "../../components/shape/src/shape.hpp"

sdl::sdl() 
{ 
    this->Handle = "sdl";
}

sdl::sdl(Json::Value config)
{
    this->Handle = "sdl";
    if(config["scale"].asString().size()) this->scale = config["scale"].asFloat();
    this->title = config["title"].asString();
    this->images = config["images"];
}

void sdl::Init()
{
    this->ComponentRequest("texture");
    this->ComponentRequest("shape");
    this->ComponentRequest("position");

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

    SDL_SetWindowFullscreen((SDL_Window *)this->window, SDL_WINDOW_FULLSCREEN_DESKTOP);

    this->renderer = SDL_CreateRenderer((SDL_Window *)this->window, -1, SDL_RENDERER_ACCELERATED);

    this->screen_surface = SDL_GetWindowSurface((SDL_Window *)this->window);

    for(uint16_t counter = 0; counter < this->images.size(); counter++)
    {
        std::string filename = this->images[counter].asString();
        this->image_cache[filename] = IMG_Load(filename.c_str());
        this->tex_cache[filename] = SDL_CreateTextureFromSurface((SDL_Renderer *)this->renderer, 
                                        (SDL_Surface *)this->image_cache[filename]);
    }
}

Json::Value sdl::save()
{
    throw std::string("save() not implemented");
    Json::Value config;
    return config;
}

void sdl::Update(uint32_t dt)
{
    if(!running)
    {
        SDL_DestroyWindow((SDL_Window *)this->window);
        SDL_Quit();
        this->Container->ManagerGet()->Shutdown();
        return;
    }

    if(this->height == 0)
    {
        int w, h;
        SDL_GetRendererOutputSize((SDL_Renderer *)this->renderer, &w, &h);
        this->height = h;
        this->width = w;
    }

    SDL_SetRenderDrawColor((SDL_Renderer *)this->renderer, 0, 0, 0, 0xFF );
    SDL_RenderClear((SDL_Renderer *)this->renderer);

    ecs::TypeEntityComponentList Components = this->ComponentsGet();

    std::shared_ptr<texture> tex;
    for(auto &entity_component_list : Components["texture"])
    {
        ecs::ComponentList texes = entity_component_list.second;
        for(auto &t : texes)
        {
            tex = std::dynamic_pointer_cast<texture>(t);
            ecs::ComponentList positions = Components["position"][t->EntityHandle];
            std::shared_ptr<position> pos;
            for(auto &p : positions)
            {
                pos = std::dynamic_pointer_cast<position>(p);
            }

            SDL_Rect src = { tex->col * tex->width, tex->row * tex->height, tex->width, tex->height };
            SDL_Rect dest = { pos->x * (tex->width * this->scale), pos->y * (tex->height * this->scale), tex->width * this->scale, tex->height * this->scale };
            SDL_SetTextureColorMod((SDL_Texture *)this->tex_cache[tex->tex_filename], tex->r, tex->g, tex->b);
            SDL_RenderCopy((SDL_Renderer *)this->renderer, (SDL_Texture *)this->tex_cache[tex->tex_filename], &src, &dest);
        }
    }

    std::shared_ptr<shape> s;
    for(auto &entity_component_list : Components["shape"])
    {
        ecs::ComponentList shapes = entity_component_list.second;
        for(auto &shape_ : shapes)
        {
            s = std::dynamic_pointer_cast<shape>(shape_);
            ecs::ComponentList positions = Components["position"][s->EntityHandle];
            std::shared_ptr<position> pos = nullptr;
            
            for(auto &p : positions)
            {
                pos = std::dynamic_pointer_cast<position>(p);
            }
            if(pos == nullptr) continue;
            
            SDL_Rect rect = { pos->x * this->scale, pos->y * this->scale, s->width * this->scale, s->height * this->scale };
            SDL_SetRenderDrawColor((SDL_Renderer *)this->renderer, s->r, s->g, s->b, s->a);
            SDL_RenderFillRect((SDL_Renderer *)this->renderer, &rect);
        }
    }

    SDL_SetRenderDrawBlendMode((SDL_Renderer *)this->renderer, SDL_BLENDMODE_BLEND);
    SDL_RenderPresent((SDL_Renderer *)this->renderer);
}

extern "C"
{
    ecs::System *create_system(void *p)
    {
        if(p == nullptr) return new sdl();

        Json::Value *config = (Json::Value *)p;
        return new sdl(*config);
    }

    void *get_system()
    {
        return (void *)create_system;
    }
}
