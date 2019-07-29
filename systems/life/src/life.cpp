#include <life.hpp>
#include <iostream>
#include "../../components/shape/src/shape.hpp"
#include "../../components/input/src/input.hpp"

life::life() 
{ 
    this->Handle = "life";
}

life::life(Json::Value config)
{
    this->Handle = "life";
}

void life::Init()
{
    this->ComponentRequest("cell");
}

Json::Value life::save()
{
    Json::Value config;
    return config;
}

void life::Update(uint32_t dt)
{
    bool update = false;

    this->ms += dt;
    std::vector<std::string> get_components;
    get_components.push_back("input");
    std::map<std::string, ecs::ComponentList> Components = this->Container->ComponentsGet(get_components);

    for(auto &c : this->to_die) c->alive = false;
    this->to_die.clear();
    for(auto &c : this->to_live) c->alive = true;
    this->to_live.clear();

    while(!Components["input"].empty())
    {
        auto c = Components["input"].back();
        Components["input"].pop_back();
        auto i = std::dynamic_pointer_cast<input>(c);

        if(i->action == "left_click")
        {
            uint32_t x = i->content["x"].asUInt() / 40;
            uint32_t y = i->content["y"].asUInt() / 40;
            this->to_invert[x][y] = "invert";
            this->ms = 0;
        }

        if(i->action == "keyup")
        {
            if(i->content["key"] == " ") this->paused = !this->paused;
        }

        if(i->action == "quit")
        {
            this->Container->ManagerGet()->Shutdown();
        }

        ecs::Entity *e = this->Container->Entity(i->EntityHandle);
        e->destroy();
        this->ms = 0;
        return;
    }

    if(this->ms > 1000)
    {
        if(!this->paused) update = true;
        this->ms -= 1000;
    }

    Components = this->ComponentsGet();

    for(auto &component : Components["cell"])
    {
        auto c = std::dynamic_pointer_cast<cell>(component);

        uint8_t neighbors = 0;
        if(update)
        {
            // Count live neighbors
            for(int8_t x = -1; x < 2; x++)
            {
                for(int8_t y = -1; y < 2; y++)
                {
                    int8_t adjusted_x = c->x + x;
                    int8_t adjusted_y = c->y + y;

                    if((x == 0) && (y == 0)) continue;
                    for(auto &check : Components["cell"])
                    {
                       auto check_cell = std::dynamic_pointer_cast<cell>(check);
                       if((check_cell->x == adjusted_x) && (check_cell->y == adjusted_y))
                       {   
                           if(check_cell->alive) neighbors++;
                       }
                    }
                }
            }
        }

        bool die = false, live = false;
        if(c->alive)
        {
            if(update)
            {
                if(neighbors < 2) die = true;
                if(neighbors > 3) die = true;
            }
            if(this->to_invert[c->x][c->y] == "invert")
            {
                die = true;
                this->to_invert[c->x][c->y] = "";
            }
        }
        else
        {
            if(update)
            { 
                if(neighbors == 3) live = true;
            }
            if(this->to_invert[c->x][c->y] == "invert") 
            {
                live = true;
                this->to_invert[c->x][c->y] = "";
            }
        }

        if(die) this->to_die.push_back(c);
        if(live) this->to_live.push_back(c);

        auto s = std::dynamic_pointer_cast<shape>(this->Container->Entity(c->EntityHandle)->ComponentGet("shape"));
        if(c->alive)
        {
            s->a = 255;
        }
        else
        {
            s->a = 16;
        }
    }
}

extern "C"
{
    ecs::System *create_system(void *p)
    {
        if(p == nullptr) return new life();

        Json::Value *config = (Json::Value *)p;
        return new life(*config);
    }
}
