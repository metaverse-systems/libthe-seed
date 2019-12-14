#pragma once

#include <libecs-cpp/ecs.hpp>
#include "zzt_world.hpp"
#include "zzt_board.hpp"
#include "zzt_colors.hpp"
#include "zzt_elements.hpp"

class zzt_engine : public ecs::System
{
  public:
    zzt_engine(); 
    zzt_engine(Json::Value);
    Json::Value Export();
    std::string filename;
    std::string tex_sheet;
    void Update(uint32_t dt);
    ecs::Entity *create_entity();
    void create_position(ecs::Entity *e, float x, float y);
    void create_texture(ecs::Entity *e, std::string filename, int32_t width, int32_t height, int32_t col, int32_t row, std::string color);
    void create_zzt_texture(ecs::Entity *e, uint8_t character, uint8_t color);
    uint16_t tex_width, tex_height;
  private:
    uint16_t cycle = 0;
    World *world = nullptr;
    Board *board = nullptr;
    uint16_t board_number = 0;
};
