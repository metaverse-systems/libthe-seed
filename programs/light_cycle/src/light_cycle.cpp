#include "light_cycle.hpp"
#include "../../systems/sdl_video/src/sdl_video.hpp"
#include <iostream>
#include <fstream>

int main(int argc, char *argv[])
{
    auto world = ECS->Container();

    // Video output
    Json::Value video_config;
    video_config["title"] = "light cycle";
    video_config["fullscreen"] = true;
    auto video = (sdl_video *)SystemLoader::Create("sdl_video", &video_config);
    world->System(video);

    // Game engine
    Json::Value cycle_config;
    cycle_config["paused"] = true;
    cycle_config["max_velocity"] = 750.0;
    world->System(SystemLoader::Create("cycle", &cycle_config));

    // Input
    world->System(SystemLoader::Create("sdl_input"));

    Json::Value gui_config;
    gui_config["visible"] = true;
    world->System(SystemLoader::Create("gui", &gui_config));

    world->Start(1000000 / 60);
    while(!video->height) usleep(100000);
    
    // Load entities from file
    JSONLoader::FileParse(world, "world.json");

    Json::StreamWriterBuilder builder;
    const std::string output = Json::writeString(builder, world->Export());
    std::cout << output << std::endl;

    while(ECS->IsRunning())
    {
        usleep(150000);
    }
    return 0;
}
