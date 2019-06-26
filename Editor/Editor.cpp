#include <libecs-cpp/ecs.hpp>
#include "../Loaders/ComponentLoader.hpp"
#include "../Loaders/SystemLoader.hpp"
#include <iostream>
#include <fstream>
#include <unistd.h>

int main(int argc, char *argv[])
{
    ecs::Container *world = ECS->Container();
    world->Start();

    Json::Value sdl;
    sdl["width"] = 1280;
    sdl["height"] = 720;
    sdl["title"] = "Editor";

    world->System(SystemLoader::Create("sdl_linux", &sdl));
    world->System(SystemLoader::Create("physics", &sdl));

    if(argc == 2)
    {
        std::ifstream file(argv[1]);
        Json::Reader reader;
        Json::Value config;
        reader.parse(file, config);

        uint16_t counter = 0;

        for(uint16_t counter = 0; counter < config.size(); counter++)
        {
            ecs::Entity *e = world->Entity(config[counter]["Handle"].asString());

            std::vector<std::string> keys = config[counter]["Components"].getMemberNames();
            for(auto &k : keys)
            {
                ecs::Component *c = nullptr;
                try
                {
                    c = ComponentLoader::Create(k, &config[counter]["Components"][k]);
                }
                catch(std::string error)
                {
                    std::cout << error << std::endl;
                    return -1;
                }

                std::cout << "Loaded component " << c->Type << std::endl;
                e->Component(c);
            }

            std::cout << "Loaded entity " << e->HandleGet() << std::endl;
        }
    }

    while(ECS->IsRunning())
    {
        usleep(50);
    }
    return 0;
}
