#include <libecs-cpp/ecs.hpp>
#include "../Loaders/ComponentLoader.hpp"
#include "../Loaders/SystemLoader.hpp"
#include <iostream>
#include <fstream>
#include <unistd.h>

int main(int argc, char *argv[])
{
    ecs::Container *world = ECS->Container();

    Json::Value sdl;
    sdl["width"] = 1280;
    sdl["height"] = 720;
    sdl["title"] = "Life";

    world->System(SystemLoader::Create("sdl_linux", &sdl));
    world->System(SystemLoader::Create("life"));

    Json::Value shape;
    shape["width"] = 38;
    shape["height"] = 38; 

    for(uint8_t x = 0; x < 32; x++)
    {
        for(uint8_t y = 0; y < 18; y++)
        {
            ecs::Entity *e = world->Entity();

            Json::Value config;

            config["x"] = x;
            config["y"] = y;
            config["alive"] = (rand() % 100) > 50 ? true : false;
            e->Component(ComponentLoader::Create("cell", &config));

            shape["x"] = x * 40;
            shape["y"] = y * 40;
            shape["r"] = rand() % 255;
            shape["g"] = rand() % 255;
            shape["b"] = rand() % 255;

            if(config["alive"].asBool())
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

    world->Start();

    while(ECS->IsRunning())
    {
        usleep(150000);
    }
    return 0;
}
