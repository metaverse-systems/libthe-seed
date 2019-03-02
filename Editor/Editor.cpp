#include <libecs-cpp/ecs.hpp>
#include <iostream>
#include <unistd.h>

auto ECS = new ecs::Manager();

class position_component : public ecs::Component
{
  public:
    float x = 0.0, y = 0.0;

    position_component(float x, float y)
    {
        this->Type = "position";
        this->x = x;
        this->y = y;
    }
};

class velocity_component : public ecs::Component
{
  public:
    float x = 0.0, y = 0.0;

    velocity_component(float x, float y)
    {
        this->Type = "velocity";
        this->x = x;
        this->y = y;
    }
};

class physics_system : public ecs::System
{
  public:
    physics_system()
    {
        this->Handle = "physics";
    }

    void Update(uint32_t dt)
    {
        const float multiplier = (float)dt / 1000.0;

        std::vector<std::string> RequestedComponents;
        RequestedComponents.push_back("position");
        std::map<std::string, std::map<std::string, ecs::Component *>> Components = this->Container->ComponentsGet(RequestedComponents);

        RequestedComponents.clear();
        RequestedComponents.push_back("velocity");

        for(auto &c : Components["position"])
        {
            auto p = (position_component *)c.second;
            std::cout << "x: " << p->x << ", y: " << p->y << std::endl;

            auto e = this->Container->Entity(p->EntityHandle);
            std::map<std::string, std::map<std::string, ecs::Component *>> eComponents = e->ComponentsGet(RequestedComponents);

            for(auto &c2 : eComponents["velocity"])
            {
                auto v = (velocity_component *)c2.second;
                std::cout << "velocity x: " << v->x << ", y: " << v->y << ". multiplier: " << multiplier << std::endl;
                p->x += multiplier * v->x;
                p->y += multiplier * v->y;
            }
        }
    }
};

int main(int argc, char *argv[])
{
    ecs::Container *level1 = ECS->Container();
//    auto level2 = ECS->Container();

    level1->System(new physics_system());
//    level2->System(new physics_system());

    ecs::Entity *e = level1->Entity("Player");
    e->Component(new position_component(10, 50));
    e->Component(new velocity_component(1, 0));

    for(;;)
    {
        usleep(50);
    }
    return 0;
}
