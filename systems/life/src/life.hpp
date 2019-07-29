#pragma once

#include <libecs-cpp/ecs.hpp>
#include "../../components/cell/src/cell.hpp"

class life : public ecs::System
{
  public:
    life(); 
    life(Json::Value);
    Json::Value save();
    void Update(uint32_t dt);
    void Init();
  private:
    uint32_t ms = 0;
    bool paused = false;
    std::vector<std::shared_ptr<cell>> to_die;
    std::vector<std::shared_ptr<cell>> to_live;
    Json::Value to_invert;
};
