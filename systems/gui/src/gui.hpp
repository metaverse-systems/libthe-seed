#pragma once

#include <libecs-cpp/ecs.hpp>

typedef struct
{
    uint16_t x, y;
    bool left_button, right_button;

    std::string hot_item;
    std::string active_item;
} mouse_state;

class gui : public ecs::System
{
  public:
    gui(); 
    gui(Json::Value);
    Json::Value save();
    bool visible = false;
    void Update();
};
