#pragma once

#include <libecs-cpp/ecs.hpp>

class gui : public ecs::System
{
  public:
    gui(); 
    gui(Json::Value);
    Json::Value save();
    uint64_t data = 0;
    void Update();
};
