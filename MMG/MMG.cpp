#include <libecs-cpp/ecs.hpp>
#include "../Loaders/ComponentLoader.hpp"
#include "../Loaders/SystemLoader.hpp"
#include <iostream>
#include <fstream>
#include <unistd.h>

constexpr auto tex_sheet = "cp437_9x16.png";
constexpr auto tex_width = 9;
constexpr auto tex_height = 16;

#if 0
constexpr auto tex_sheet = "cp437_10x10.png";
constexpr auto tex_width = 10;
constexpr auto tex_height = 10;
#endif

#if 0
constexpr auto tex_sheet = "cp437_16x16.png";
constexpr auto tex_width = 16;
constexpr auto tex_height = 16;
#endif

#if 0
constexpr auto tex_sheet = "cp437_32x48.png";
constexpr auto tex_width = 32;
constexpr auto tex_height = 48;
#endif

constexpr auto scale = 2;

int main(int argc, char *argv[])
{
    ecs::Container *world = ECS->Container();

    Json::Value sdl;
    sdl["width"] = tex_width * 80 * scale;
    sdl["height"] = tex_height * 25 * scale;
    sdl["scale"] = scale;
    sdl["columns"] = 80;
    sdl["rows"] = 25;
    sdl["title"] = "MMG";
    sdl["images"].append(tex_sheet);
    world->System(SystemLoader::Create("sdl_linux", &sdl));

    Json::Value zzt_config;
    zzt_config["filename"] = "./TOWN.ZZT";
    zzt_config["board"] = 0;
    zzt_config["tex_sheet"] = tex_sheet;
    zzt_config["tex_width"] = tex_width;
    zzt_config["tex_height"] = tex_height;

    if(argc >= 2) zzt_config["filename"] = argv[1];
    if(argc > 2) zzt_config["board"] = std::stoi(argv[2]);
    std::cout << "Loading " << zzt_config["filename"] << std::endl;
    world->System(SystemLoader::Create("zzt_engine", &zzt_config));

//    world->Start(1000000 / 15);
    world->SystemsInit();

    while(ECS->IsRunning())
    {
        usleep(150000);
        world->Update();
    }
    return 0;
}
