#include "Life.hpp"
#include "../../../systems/sdl/src/sdl.hpp"
#include "../../../systems/life2/src/life2.hpp"
#include <iostream>
#include <fstream>

constexpr auto scale = 1;
constexpr auto song = "resource1/2AM_in_Shimokitazawa_1.0.mp3";

int main(int argc, char *argv[])
{
    ecs::Container *world = ECS->Container();

    ResourcePak *r1 = new ResourcePak("resource1");

    Json::Value sdl_config;
    sdl_config["title"] = "Life";
    sdl_config["scale"] = scale;

    sdl *sdl_system = (sdl *)SystemLoader::Create("sdl", &sdl_config);
    world->System(sdl_system);

    world->System(SystemLoader::Create("sdl_input"));

    Json::Value shape;
    shape["width"] = 30;
    shape["height"] = 30;

    ecs::System *sdl_mixer_system = SystemLoader::Create("sdl_mixer");
    world->System(sdl_mixer_system);

    world->Start(1000000 / 10);

    while(sdl_system->height == 0)
    {
        usleep(50000);
    }

    shape["columns"] = sdl_system->width / shape["width"].asUInt();
    shape["rows"] = sdl_system->height / shape["height"].asUInt();

    sdl_mixer_system->ResourceAdd(r1->name, r1);

    Json::Value song;
    song["resource_pak"] = "resource1";
    song["name"] = "2AM in Shimokitazawa 1.0";
    song["status"] = "start_playing";

    ecs::Entity *e = world->Entity();
    e->Component(ComponentLoader::Create("song", &song));

    for(uint16_t x = 0; x < (sdl_system->width / shape["width"].asUInt()); x++)
    {
        for(uint16_t y = 0; y < (sdl_system->height / shape["height"].asUInt()); y++)
        {
//            std::cout << "Creating cell " << std::to_string(x) << ", " << std::to_string(y) << std::endl;
            e = world->Entity();

            Json::Value cell;

            cell["x"] = x;
            cell["y"] = y;
            cell["alive"] = (rand() % 100) > 85 ? true : false;
            e->Component(ComponentLoader::Create("cell", &cell));

            Json::Value pos;
            pos["x"] = x * shape["width"].asUInt();
            pos["y"] = y * shape["height"].asUInt();
            e->Component(ComponentLoader::Create("position", &pos));
            shape["x"] = x * shape["width"].asUInt();
            shape["y"] = y * shape["height"].asUInt();
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

    life2 *life_system = (life2 *)SystemLoader::Create("life2", &shape);
    world->System(life_system);
    life_system->Init();

    while(ECS->IsRunning())
    {
        usleep(500000);
    }
    return 0;
}
