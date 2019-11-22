#include <life2.hpp>
#include "../../components/shape/src/shape.hpp"
#include "../../components/input/src/input.hpp"
#include <iostream>

life2::life2() 
{ 
    this->Handle = "life2";
}

life2::life2(Json::Value config)
{
    this->Handle = "life2";
    this->cell_width = config["width"].asUInt();
    this->cell_height = config["height"].asUInt();
    this->cell_columns = config["columns"].asUInt();
    this->cell_rows = config["rows"].asUInt();
}

void life2::Init()
{
    this->ComponentRequest("cell");
    this->ComponentRequest("shape");
    this->ComponentRequest("input");

    this->cells = new std::shared_ptr<cell>*[this->cell_columns]; 
    for(uint32_t column = 0; column < this->cell_columns; column++)
    {
        this->cells[column] = new std::shared_ptr<cell>[this->cell_rows];
    }
}

Json::Value life2::save()
{
    Json::Value config;
    return config;
}

std::shared_ptr<cell> life2::CellGet(uint32_t x, uint32_t y)
{
    ecs::TypeEntityComponentList Components = this->ComponentsGet();

    for(auto &c : Components["cell"])
    {
        while(auto component = c.second.Pop())
        {
            auto result = std::dynamic_pointer_cast<cell>(component);
            if((result->x == x) && (result->y == y)) return result;
        }
    }

    return nullptr;
}

void life2::Update()
{
    bool update = false;
    auto dt = this->DeltaTimeGet();

    for(auto &c : this->to_die) c->alive = false;
    this->to_die.clear();
    for(auto &c : this->to_live) c->alive = true;
    this->to_live.clear();

    this->ms += dt;
    ecs::TypeEntityComponentList Components = this->ComponentsGet();

    for(auto &[entity, component_list] : Components["input"])
    {
        while(auto component = component_list.Pop())
        {
            auto i = std::dynamic_pointer_cast<input>(component);

            if(i->action == "left_click")
            {
                uint32_t x = i->content["x"].asUInt() / this->cell_width;
                uint32_t y = i->content["y"].asUInt() / this->cell_height;
                this->to_invert[x][y] = "invert";
                this->ms = 0;
            }

            if(i->action == "keyup")
            {
                if(i->content["key"] == "Space") this->paused = !this->paused;
                if(i->content["key"] == "Escape") this->Container->ManagerGet()->Shutdown();
            }

            ecs::Entity *e = this->Container->Entity(entity);
            e->destroy();
            return;
        }
    }

    if(this->ms > 1000)
    {
        if(!this->paused) update = true;
        this->ms -= 1000;
    }

    for(uint16_t column = 0; column < this->cell_columns; column++)
    {
        for(uint16_t row = 0; row < this->cell_rows; row++)
        {
//            std::cout << "Checking cell " << std::to_string(column) << ", " << std::to_string(row) << std::endl;
            if(this->cells == nullptr) return;
            if(this->cells[column] == nullptr) return;
            if(this->cells[column][row] == nullptr)
            {
                this->cells[column][row] = this->CellGet(column, row);
            }

            std::shared_ptr<cell> c = this->cells[column][row];
            if(c == nullptr) continue;

            uint8_t neighbors = 0;
            if(update)
            {
                // Count live neighbors
                for(int8_t x = -1; x < 2; x++)
                {
                    for(int8_t y = -1; y < 2; y++)
                    {
                        int16_t adjusted_x = c->x + x;
                        int16_t adjusted_y = c->y + y;

                        if(adjusted_x == this->cell_columns) adjusted_x = 0;
                        if(adjusted_x < 0) adjusted_x = this->cell_columns - 1;

                        if(adjusted_y == this->cell_rows) adjusted_y = 0;
                        if(adjusted_y < 0) adjusted_y = this->cell_rows - 1;

                        if((x == 0) && (y == 0)) continue;
                        if(this->cells[adjusted_x] == nullptr) continue;

                        auto check_cell = this->cells[adjusted_x][adjusted_y];
                        if(check_cell == nullptr) continue;
                        if(check_cell->alive) neighbors++;
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

            auto s = std::dynamic_pointer_cast<shape>(Components["shape"][c->EntityHandle].Pop());
            if(s == nullptr) continue;

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
}

extern "C"
{
    ecs::System *create_system(void *p)
    {
        if(p == nullptr) return new life2();

        Json::Value *config = (Json::Value *)p;
        return new life2(*config);
    }
}
