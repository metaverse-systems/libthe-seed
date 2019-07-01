#pragma once
#include <string>
#include <vector>

typedef struct
{
        std::string name = "Unimplemented";
        char character = 0;
        uint8_t col, row;
} zzt_element;

extern std::vector<zzt_element> zzt_elements;

void init_zzt_elements();
