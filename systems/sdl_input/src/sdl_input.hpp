#pragma once

#include <libecs-cpp/ecs.hpp>

class sdl_input : public ecs::System
{
  public:
    sdl_input(); 
    sdl_input(Json::Value);
    Json::Value save();
    void Update();
    void Init();
  private:
    std::vector<std::string> registered_keys;
};
