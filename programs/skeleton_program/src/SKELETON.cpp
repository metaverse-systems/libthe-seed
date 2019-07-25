#include "SKELETON.hpp"

int main(int argc, char *argv[])
{
    ecs::Container *world = ECS->Container();



    world->Start(1000000 / 15);

    while(ECS->IsRunning())
    {
        usleep(150000);
    }
    return 0;
}
