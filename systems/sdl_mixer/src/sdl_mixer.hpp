#pragma once

#include <libecs-cpp/ecs.hpp>
#include "../../loaders/ResourcePak.hpp"

class sdl_mixer : public ecs::System
{
  public:
    sdl_mixer(); 
    sdl_mixer(Json::Value);
    Json::Value save();
    void Update();
    void Init();
};
