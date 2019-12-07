#include <cycle.hpp>
#include "../../components/input/src/input.hpp"
#include "../../components/position/src/position.hpp"
#include "../../components/velocity/src/velocity.hpp"
#include "../../components/cycle_shape/src/cycle_shape.hpp"
#include "../../components/draw/src/draw.hpp"
#include <cmath>
#include <iostream>

cycle::cycle() 
{ 
    this->Handle = "cycle";
}

cycle::cycle(Json::Value config)
{
    this->Handle = "cycle";
    if(config["paused"] != "") this->paused = config["paused"].asBool();
}

Json::Value cycle::save()
{
    Json::Value config;
    return config;
}

void cycle::Init()
{
    this->ComponentRequest("input");
    this->ComponentRequest("position");
    this->ComponentRequest("velocity");
    this->ComponentRequest("cycle_shape");
    this->ComponentRequest("draw");
}

void cycle::Update()
{
    auto turn_right = false;
    auto turn_left = false;
    auto turn_up = false;
    auto turn_down = false;
    auto dt = this->DeltaTimeGet();
    float multiplier = dt / 1000.0;

    auto Components = this->ComponentsGet();
    for(auto &[entity, component_list] : Components["input"])
    {
        while(auto component = component_list.Pop())
        {
            auto i = std::dynamic_pointer_cast<input>(component);
    
            if(i->event == "keyup")
            {
                if(i->content["key"] == "Space") this->paused = !this->paused;
                if(i->content["key"] == "Escape") this->Container->ManagerGet()->Shutdown();

                if(i->content["key"] == "Right") turn_right = true;
                if(i->content["key"] == "Left") turn_left = true;
                if(i->content["key"] == "Up") turn_up = true;
                if(i->content["key"] == "Down") turn_down = true;
            }
        }

        this->Container->Entity(entity)->destroy();
    }

    if(this->paused) return;

    for(auto &[entity, component_list] : Components["position"])
    {
        while(auto pcomponent = component_list.Pop())
        {
            auto pos = std::dynamic_pointer_cast<position>(pcomponent);

            auto vcomponent = Components["velocity"][entity].Pop();
            if(nullptr == vcomponent) continue;

            auto vel = std::dynamic_pointer_cast<velocity>(vcomponent);

            pos->x += vel->x * multiplier;
            pos->y += vel->y * multiplier;

            vel->x *= 1.0005;
            vel->y *= 1.0005;

            if(turn_left || turn_right || turn_up || turn_down)
            {
                auto scomponent = Components["cycle_shape"][entity].Pop();
                auto s = std::dynamic_pointer_cast<cycle_shape>(scomponent);

                auto dcomponent = Components["draw"][entity].Pop();
                auto d = std::dynamic_pointer_cast<draw>(dcomponent);

                float velocity = vel->x ? std::abs(vel->x) : std::abs(vel->y);


                if(turn_up)
                {
                    if(vel->y > 0) continue;

                    d->width = s->height;
                    d->height = s->width;
                    vel->x = 0;
                    vel->y = velocity * -1;
                }

                if(turn_down)
                {
                    if(vel->y < 0) continue;

                    d->width = s->height;
                    d->height = s->width;
                    vel->x = 0;
                    vel->y = velocity;
                }

                if(turn_left)
                {
                    if(vel->x > 0) continue;

                    d->width = s->width;
                    d->height = s->height;
                    vel->x = velocity * -1;
                    vel->y = 0;
                }

                if(turn_right)
                {
                    if(vel->x < 0) continue;

                    d->width = s->width;
                    d->height = s->height;
                    vel->x = velocity;
                    vel->y = 0;
                }
            }
        }
    }
}

extern "C"
{
    ecs::System *create_system(void *p)
    {
        if(p == nullptr) return new cycle();

        Json::Value *config = (Json::Value *)p;
        return new cycle(*config);
    }
}
