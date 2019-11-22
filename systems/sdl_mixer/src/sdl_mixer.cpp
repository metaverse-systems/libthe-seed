#include <sdl_mixer.hpp>
#include <iostream>
#include <fstream>
#include <SDL.h>
#include <SDL_mixer.h>
#include "../../components/song/src/song.hpp"

bool song_finished = false;
Mix_Music *music;

void finish_song()
{
    song_finished = true;
}

sdl_mixer::sdl_mixer() 
{ 
    this->Handle = "sdl_mixer";
}

sdl_mixer::sdl_mixer(Json::Value config)
{
    this->Handle = "sdl_mixer";
}

Json::Value sdl_mixer::save()
{
    Json::Value config;
    return config;
}

void sdl_mixer::Init()
{
    this->ComponentRequest("song");

    if(SDL_Init(SDL_INIT_AUDIO) < 0) 
    {
        std::string err = "Couldn't initialize SDL audio";
        throw std::runtime_error(err);
    }

    Mix_OpenAudio(22050, AUDIO_S16SYS, 2, 4096);

    int result = 0, flags = MIX_INIT_MP3;
    if(flags != (result = Mix_Init(flags))) 
    {
        std::string err = "Could not initialize mixer, result: " + std::to_string(result);
        err += std::string("Mix_Init: ") + Mix_GetError();
        throw std::runtime_error(err);
    }
}

void sdl_mixer::Update()
{
    ecs::TypeEntityComponentList Components = this->ComponentsGet();

    for(auto &[entity, component_list] : Components["song"])
    {
        auto s = std::dynamic_pointer_cast<song>(component_list.Pop());
        if(s == nullptr) continue;

        if(s->status == "start_playing")
        {
            ResourcePak *p = (ResourcePak *)this->resources[s->resource_pak];
            if(p == nullptr) continue;

            s->status = "playing";
            Resource r = p->get(s->name);

            SDL_RWops *rw = SDL_RWFromMem(r.ptr, r.size);
            if(rw == nullptr)
            {
                std::cout << "SDL_RWFromMem failed" << std::endl;
            }
            music = Mix_LoadMUS_RW(rw, 1);
            Mix_PlayMusic(music, 1);
            Mix_HookMusicFinished(finish_song);
            continue;
        }

        if(song_finished)
        {
            Mix_HaltMusic();
            Mix_FreeMusic(music);
            song_finished = false;
            s->status = "start_playing";
        }
    }
}

extern "C"
{
    ecs::System *create_system(void *p)
    {
        if(p == nullptr) return new sdl_mixer();

        Json::Value *config = (Json::Value *)p;
        return new sdl_mixer(*config);
    }
}
