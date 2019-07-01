#pragma once
#include <map>
#include <string>

typedef struct
{
        uint8_t r, g, b;
} zzt_color;    

extern std::map<std::string, zzt_color> zzt_palette;
extern std::map<uint8_t, std::string> zzt_colors;

void init_zzt_palette();
