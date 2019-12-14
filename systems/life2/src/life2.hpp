#pragma once

#include <libecs-cpp/ecs.hpp>
#include "../../components/cell/src/cell.hpp"

class life2 : public ecs::System
{
  public:
    life2(); 
    life2(Json::Value);
    Json::Value Export();
    void Update();
    void Init();
  private:
    uint32_t ms = 0;
    bool paused = false;
    std::shared_ptr<cell> **cells = nullptr;
    std::vector<std::shared_ptr<cell>> to_die;
    std::vector<std::shared_ptr<cell>> to_live;
    Json::Value to_invert;
    uint32_t cell_width = 0, cell_height = 0;
    uint32_t cell_columns = 0, cell_rows = 0;
    std::shared_ptr<cell> CellGet(uint32_t, uint32_t);
};
