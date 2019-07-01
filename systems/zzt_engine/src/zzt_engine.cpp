#include <zzt_engine.hpp>
#include "../../components/position/src/position.hpp"
#include "../../../Loaders/ComponentLoader.hpp"
#include <iostream>

zzt_engine::zzt_engine() 
{ 
    this->Handle = "zzt_engine";
}

zzt_engine::zzt_engine(Json::Value config)
{
    this->Handle = "zzt_engine";
    this->filename = config["filename"].asString();
    this->tex_sheet = config["tex_sheet"].asString();
    this->board_number = config["board"].asUInt();

    this->ComponentRequest("position");
}

Json::Value zzt_engine::save()
{
    Json::Value config;
    return config;
}

void zzt_engine::Update(uint32_t dt)
{
    // 110ms == 9.1Hz
    this->cycle += dt;
    if(this->cycle < 110) return;
    this->cycle -= 110;

    if(this->world == nullptr) this->world = new World(this->filename);
    if(this->board == nullptr)
    {
        init_zzt_palette();
        init_zzt_elements();
        
        this->board = new Board(this->world->GetBoard(this->board_number), this);
    }

    ecs::ComponentMap Components = this->ComponentsGet();

    for(auto &component : Components["position"])
    {
        auto pos = (position *)component.second;
    }
}

ecs::Entity *zzt_engine::create_entity()
{
    return this->Container->Entity();
}

void zzt_engine::create_position(ecs::Entity *e, float x, float y)
{
    Json::Value pos_config;
    pos_config["x"] = x;
    pos_config["y"] = y;
    e->Component(ComponentLoader::Create("position", &pos_config));
}

void zzt_engine::create_texture(ecs::Entity *e, std::string filename, int32_t width, int32_t height, int32_t col, int32_t row, std::string color)
{
    Json::Value tex_config;
    tex_config["tex_filename"] = filename;
    tex_config["width"] = width;
    tex_config["height"] = height;
    tex_config["col"] = col;
    tex_config["row"] = row;
    tex_config["r"] = zzt_palette[color].r;
    tex_config["g"] = zzt_palette[color].g;
    tex_config["b"] = zzt_palette[color].b;
    e->Component(ComponentLoader::Create("texture", &tex_config));
}

void zzt_engine::create_zzt_texture(ecs::Entity *e, uint8_t character, uint8_t color)
{
    if(zzt_elements[character].character == 0)
    {
        std::cout << "Can't find character " << std::to_string(character) << std::endl;
    }
    this->create_texture(e, this->tex_sheet, 9, 16,
                         zzt_elements[character].col,
                         zzt_elements[character].row,
                         zzt_colors[color]);
}

extern "C"
{
    ecs::System *create_system(void *p)
    {
        if(p == nullptr) return new zzt_engine();

        Json::Value *config = (Json::Value *)p;
        return new zzt_engine(*config);
    }
}