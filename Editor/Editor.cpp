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

    ConfigMap cm;

    cm["x"] = "50.5";
    cm["y"] = "100.0";
    e->Component(ComponentLoader::Get("position", &cm));

    cm["x"] = "0.5";
    cm["y"] = "0.0";
    e->Component(ComponentLoader::Get("velocity", &cm));

    for(;;)
    {
        usleep(50);
    }
    return 0;
}
