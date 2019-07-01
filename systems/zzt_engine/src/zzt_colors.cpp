#include <map>
#include <string>
#include "zzt_colors.hpp"

std::map<std::string, zzt_color> zzt_palette;
std::map<uint8_t, std::string> zzt_colors;

void init_zzt_palette()
{
    zzt_colors[0] = "black";
    zzt_palette["black"].r = 0;             zzt_palette["black"].g = 0;             zzt_palette["black"].b = 0;

    zzt_colors[1] = "blue";
    zzt_palette["blue"].r = 0;              zzt_palette["blue"].g = 0;              zzt_palette["blue"].b = 170;

    zzt_colors[2] = "green";
    zzt_palette["green"].r = 0;             zzt_palette["green"].g = 170;           zzt_palette["green"].b = 0;

    zzt_colors[3] = "cyan";
    zzt_palette["cyan"].r = 0;              zzt_palette["cyan"].g = 170;            zzt_palette["cyan"].b = 170;

    zzt_colors[4] = "red";
    zzt_palette["red"].r = 170;             zzt_palette["red"].g = 0;               zzt_palette["red"].b = 0;

    zzt_colors[5] = "magenta";
    zzt_palette["magenta"].r = 170;         zzt_palette["magenta"].g = 0;           zzt_palette["magenta"].b = 170;

    zzt_colors[6] = "brown";
    zzt_palette["brown"].r = 170;           zzt_palette["brown"].g = 85;            zzt_palette["brown"].b = 0;

    zzt_colors[7] = "white";
    zzt_palette["white"].r = 170;           zzt_palette["white"].g = 170;           zzt_palette["white"].b = 170;

    zzt_colors[8] = "gray";
    zzt_palette["gray"].r = 85;             zzt_palette["gray"].g = 85;             zzt_palette["gray"].b = 85;
 
    zzt_colors[9] = "light blue";
    zzt_palette["light blue"].r = 85;       zzt_palette["light blue"].g = 85;       zzt_palette["light blue"].b = 255;

    zzt_colors[10] = "light green";
    zzt_palette["light green"].r = 85;      zzt_palette["light green"].g = 255;     zzt_palette["light green"].b = 64;

    zzt_colors[11] = "light cyan";
    zzt_palette["light cyan"].r = 85;       zzt_palette["light cyan"].g = 255;      zzt_palette["light cyan"].b = 255;

    zzt_colors[12] = "light red";
    zzt_palette["light red"].r = 255;       zzt_palette["light red"].g = 85;        zzt_palette["light red"].b = 85;

    zzt_colors[13] = "light magenta";
    zzt_palette["light magenta"].r = 255;   zzt_palette["light magenta"].g = 85;    zzt_palette["light magenta"].b = 255;

    zzt_colors[14] = "yellow";
    zzt_palette["yellow"].r = 255;          zzt_palette["yellow"].g = 255;          zzt_palette["yellow"].b = 85;

    zzt_colors[15] = "bright white";
    zzt_palette["bright white"].r = 255;    zzt_palette["bright white"].g = 255;    zzt_palette["bright white"].b = 255;
}
