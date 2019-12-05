#pragma once

#include <libecs-cpp/ecs.hpp>

class gui : public ecs::System
{
  public:
    gui(); 
    gui(Json::Value);
    Json::Value save();
    bool visible = false;
    void Update();
};
