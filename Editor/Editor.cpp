#include <libecs-cpp/ecs.hpp>
#include "../Loaders/ComponentLoader.hpp"
#include "../Loaders/SystemLoader.hpp"
#include <iostream>
#include <unistd.h>

int main(int argc, char *argv[])
{
    ecs::Container *world = ECS->Container();

    world->System(SystemLoader::Get("physics"));

    ecs::Entity *e = world->Entity("Player");

    Json::Value config;

    config["x"] = 50.5;
    config["y"] = 100.0;
    e->Component(ComponentLoader::Get("position", &config));

    config["x"] = 0.5;
    config["y"] = 0.0;
    e->Component(ComponentLoader::Get("velocity", &config));

    std::cout << world->save() << std::endl;

    for(;;)
    {
        usleep(50);
    }
    return 0;
}
