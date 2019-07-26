#include <life.hpp>
#include <iostream>
#include "../../components/cell/src/cell.hpp"
#include "../../components/shape/src/shape.hpp"

life::life() 
{ 
    this->Handle = "life";

    this->ComponentRequest("cell");
}

life::life(Json::Value config)
{
    this->Handle = "life";

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
    Json::Value to_invert;
    while(!this->messages.empty())
    {
        Json::Value message = this->messages.front();
        this->messages.pop();
        if(message["action"] == "left_click")
        {
            uint32_t x = message["x"].asUInt() / 40;
            uint32_t y = message["y"].asUInt() / 40;
            to_invert[x][y] = "invert";
            this->ms = 0;
        }
    }

    if(this->ms > 2500)
    {
        update = true;
        this->ms -= 2500;
    }

    std::map<std::string, ecs::ComponentList> Components = this->ComponentsGet();
    std::vector<cell *> to_die;
    std::vector<cell *> to_live;

    for(auto &component : Components["cell"])
    {
        auto c = (cell *)component;

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
                       auto check_cell = (cell *)check;
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
            if(to_invert[c->x][c->y] == "invert") die = true;
        }
        else
        {
            if(update)
            { 
                if(neighbors == 3) live = true;
            }
            if(to_invert[c->x][c->y] == "invert") live = true;
        }

        if(die) to_die.push_back(c);
        if(live) to_live.push_back(c);

        auto s = (shape *)this->Container->Entity(c->EntityHandle)->ComponentGet("shape");
        if(c->alive)
        {
            s->a = 255;
        }
        else
        {
            s->a = 16;
        }
    }

    for(auto &c : to_die) c->alive = false;
    for(auto &c : to_live) c->alive = true;
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
