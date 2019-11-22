#include "light_cycle.hpp"
#include "../../systems/sdl_video/src/sdl_video.hpp"

int main(int argc, char *argv[])
{
    auto world = ECS->Container();

    Json::Value video_config;
    video_config["title"] = "light cycle";
    video_config["fullscreen"] = true;
    auto video = (sdl_video *)SystemLoader::Create("sdl_video", &video_config);
    world->System(video);
    world->System(SystemLoader::Create("cycle"));
    world->System(SystemLoader::Create("sdl_input"));

    world->Start(1000000 / 60);
    
    auto e = world->Entity();

    Json::Value pos_config;
    pos_config["x"] = 15;
    pos_config["y"] = 10;
    e->Component(ComponentLoader::Create("position", &pos_config));

    Json::Value vel_config;
    vel_config["x"] = 50;
    vel_config["y"] = 0;
    e->Component(ComponentLoader::Create("velocity", &vel_config));

    Json::Value draw_config;
    draw_config["width"] = 10;
    draw_config["height"] = 10;
    draw_config["r"] = 255;
    draw_config["g"] = 113;
    draw_config["b"] = 206;
    draw_config["a"] = 255;
    e->Component(ComponentLoader::Create("draw", &draw_config));

    Json::Value cycle_shape_config;
    cycle_shape_config["width"] = 10;
    cycle_shape_config["height"] = 10;
    e->Component(ComponentLoader::Create("cycle_shape", &cycle_shape_config));

    while(!video->height) usleep(100000);

    // Set wall color
    draw_config["r"] = 255;
    draw_config["g"] = 251;
    draw_config["b"] = 150;
    draw_config["a"] = 255;

    auto top_wall = world->Entity();

    pos_config["x"] = 0;
    pos_config["y"] = 0;
    top_wall->Component(ComponentLoader::Create("position", &pos_config));

    draw_config["width"] = video->width;
    draw_config["height"] = 10;
    top_wall->Component(ComponentLoader::Create("draw", &draw_config));

    auto right_wall = world->Entity();

    pos_config["x"] = video->width - 10;
    pos_config["y"] = 0;
    right_wall->Component(ComponentLoader::Create("position", &pos_config));

    draw_config["width"] = 10;
    draw_config["height"] = video->height;
    right_wall->Component(ComponentLoader::Create("draw", &draw_config));

    draw_config["r"] = 5;
    draw_config["g"] = 255;
    draw_config["b"] = 161;
    draw_config["a"] = 255;

    auto bottom_wall = world->Entity();

    pos_config["x"] = 0;
    pos_config["y"] = video->height - 10;
    bottom_wall->Component(ComponentLoader::Create("position", &pos_config));

    draw_config["width"] = video->width;
    draw_config["height"] = 10;
    bottom_wall->Component(ComponentLoader::Create("draw", &draw_config));

    auto left_wall = world->Entity();

    pos_config["x"] = 0;
    pos_config["y"] = 0;
    left_wall->Component(ComponentLoader::Create("position", &pos_config));

    draw_config["width"] = 10;
    draw_config["height"] = video->height;
    left_wall->Component(ComponentLoader::Create("draw", &draw_config));

    while(ECS->IsRunning())
    {
        usleep(150000);
    }
    return 0;
}
