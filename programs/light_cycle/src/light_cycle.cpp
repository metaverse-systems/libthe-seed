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
    world->System(SystemLoader::Create("cycle", &cycle_config));

    // Input
    world->System(SystemLoader::Create("sdl_input"));

    Json::Value gui_config;
    gui_config["visible"] = true;
    world->System(SystemLoader::Create("gui", &gui_config));

    world->Start(1000000 / 60);
    while(!video->height) usleep(100000);
    
    std::string data;
    std::ifstream file;
    std::streampos fsize, fstart = 0;

    Json::Value config;
    config["screen_height"] = video->height;
    config["screen_width"] = video->width;

    file.open("world.json");
    fstart = file.tellg();
    file.seekg(0, std::ios::end);
    fsize = file.tellg() - fstart;
    file.seekg(0, std::ios::beg);
    data.resize(fsize);
    file.read(&data[0], fsize);
    file.close();

    JSONLoader::Parse(world, data, config);

    while(ECS->IsRunning())
    {
        usleep(150000);
    }
    return 0;
}
