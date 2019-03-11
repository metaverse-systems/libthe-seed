#include <libecs-cpp/ecs.hpp>
#include "../Loaders/ComponentLoader.hpp"
#include "../Loaders/SystemLoader.hpp"
#include <iostream>
#include <unistd.h>

int main(int argc, char *argv[])
{
    ecs::Container *world = ECS->Container();

    world->System(SystemLoader::Get("physics"));

    Json::Value wxw;
    wxw["argc"] = argc;
    wxw["argv"] = (Json::UInt64)((uint64_t)argv);
    world->System(SystemLoader::Get("wxwidgets_gui", &wxw));

    ecs::Entity *e = world->Entity("Player");

    Json::Value config;

    config["x"] = 50.5;
    config["y"] = 100.0;
    e->Component(ComponentLoader::Get("position", &config));

    config["x"] = 0.5;
    config["y"] = 0.0;
    e->Component(ComponentLoader::Get("velocity", &config));

    std::cout << world->save() << std::endl;

    while(ECS->IsRunning())
    {
        usleep(50);
    }
    return 0;
}
