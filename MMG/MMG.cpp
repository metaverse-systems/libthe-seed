#include <libecs-cpp/ecs.hpp>
#include "../Loaders/ComponentLoader.hpp"
#include "../Loaders/SystemLoader.hpp"
#include <iostream>
#include <fstream>
#include <unistd.h>

constexpr auto tex_sheet = "cp437_9x16.png";

int main(int argc, char *argv[])
{
    ecs::Container *world = ECS->Container();

    Json::Value sdl;
    sdl["width"] = 720;
    sdl["height"] = 400;
    sdl["columns"] = 80;
    sdl["rows"] = 25;
    sdl["title"] = "MMG";
    sdl["images"].append("cp437_9x16.png");

    world->System(SystemLoader::Create("sdl_linux", &sdl));

    Json::Value zzt_config;
    zzt_config["filename"] = "./TOWN.ZZT";
    zzt_config["board"] = 0;
    zzt_config["tex_sheet"] = tex_sheet;

    if(argc >= 2) zzt_config["filename"] = argv[1];
    if(argc > 2) zzt_config["board"] = std::stoi(argv[2]);
    std::cout << "Loading " << zzt_config["filename"] << std::endl;
    world->System(SystemLoader::Create("zzt_engine", &zzt_config));

#if 0
    ecs::Entity *e = nullptr;

    e = world->Entity();
    create_position(e, 10, 16);
    create_zzt_texture(e, 4, "bright white");
#endif

    world->Start(1000000 / 15);

    while(ECS->IsRunning())
    {
        usleep(150000);
    }
    return 0;
}
