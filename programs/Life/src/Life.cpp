#include "Life.hpp"
#include "../../../systems/sdl/src/sdl.hpp"
#include <iostream>

constexpr auto scale = 1;
constexpr auto song = "2AM_in_Shimokitazawa_1.0.mp3";

int main(int argc, char *argv[])
{
    ecs::Container *world = ECS->Container();

    Json::Value sdl_config;
    sdl_config["title"] = "Life";
    sdl_config["scale"] = scale;

    sdl *sdl_system = (sdl *)SystemLoader::Create("sdl", &sdl_config);
    world->System(sdl_system);
    world->System(SystemLoader::Create("life"));

    Json::Value mixer_config;
    mixer_config["song"] = song;
    world->System(SystemLoader::Create("sdl_mixer", &mixer_config));

    world->Start(1000000 / 15);

    while(sdl_system->height == 0) usleep(10000);

    Json::Value shape;
    shape["width"] = 40;
    shape["height"] = 40;

    for(uint8_t x = 0; x < (sdl_system->width / shape["width"].asUInt()); x++)
    {
        for(uint8_t y = 0; y < (sdl_system->height / shape["height"].asUInt()); y++)
        {
            ecs::Entity *e = world->Entity();

            Json::Value cell;

            cell["x"] = x;
            cell["y"] = y;
            cell["alive"] = (rand() % 100) > 75 ? true : false;
            e->Component(ComponentLoader::Create("cell", &cell));

            Json::Value pos;
            pos["x"] = x * 40;
            pos["y"] = y * 40;
            e->Component(ComponentLoader::Create("position", &pos));
            shape["x"] = x * 40;
            shape["y"] = y * 40;
            shape["r"] = rand() % 255;
            shape["g"] = rand() % 255;
            shape["b"] = rand() % 255;

            if(cell["alive"].asBool())
            {
                shape["a"] = 255;
            }
            else
            {
                shape["a"] = 32;
            }
            e->Component(ComponentLoader::Create("shape", &shape));
        }
    }

    while(ECS->IsRunning())
    {
        usleep(150000);
    }
    return 0;
}
