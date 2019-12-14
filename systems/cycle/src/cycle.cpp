#include <cycle.hpp>
#include "../../components/input/src/input.hpp"
#include "../../components/position/src/position.hpp"
#include "../../components/velocity/src/velocity.hpp"
#include "../../components/cycle_shape/src/cycle_shape.hpp"
#include "../../components/draw/src/draw.hpp"
#include "../../components/collision/src/collision.hpp"
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
    this->max_velocity = config["max_velocity"].asFloat();
}

Json::Value cycle::Export()
{
    Json::Value config;
    config["paused"] = this->paused ? "true" : "false";
    config["max_velocity"] = this->max_velocity;
    return config;
}

void cycle::Init()
{
    this->ComponentRequest("input");
    this->ComponentRequest("position");
    this->ComponentRequest("velocity");
    this->ComponentRequest("cycle_shape");
    this->ComponentRequest("draw");
    this->ComponentRequest("collision");
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

    for(auto &[entity, component_list] : Components["collision"])
    {
        while(auto ccomponent = component_list.Pop())
        {
            auto pcomponent = Components["position"][entity].Pop();
            auto dcomponent = Components["draw"][entity].Pop();
            auto pos = std::dynamic_pointer_cast<position>(pcomponent);
            auto d = std::dynamic_pointer_cast<draw>(dcomponent);

            auto CheckComponents = this->ComponentsGet();
            for(auto &[check_entity, check_list] : CheckComponents["collision"])
            {
                if(check_entity == entity) continue;

                while(auto check_ccomponent = check_list.Pop())
                {
                    auto check_pcomponent = CheckComponents["position"][check_entity].Pop();
                    if(check_pcomponent == nullptr) continue;

                    auto check_dcomponent = CheckComponents["draw"][check_entity].Pop();
                    if(check_dcomponent == nullptr) continue;

                    auto check_pos = std::dynamic_pointer_cast<position>(check_pcomponent);
                    auto check_d = std::dynamic_pointer_cast<draw>(check_dcomponent);

                    if(pos->x < check_pos->x + check_d->width &&
                       pos->x + d->width > check_pos->x &&
                       pos->y < check_pos->y + check_d->height &&
                       pos->y + d->height > check_pos->y) 
                    {
                        this->paused = true;
                        auto vcomponent = Components["velocity"][entity].Pop();
                        auto vel = std::dynamic_pointer_cast<velocity>(vcomponent);
                        std::cout << "Ending velocity " << vel->x << ", " << vel->y << std::endl;
                        return;
                    }
                }
            }
        }
    }

    Components = this->ComponentsGet();

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

            if((float)std::abs(vel->x) < this->max_velocity) vel->x *= 1.0015;
            if((float)std::abs(vel->y) < this->max_velocity) vel->y *= 1.0015;

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
