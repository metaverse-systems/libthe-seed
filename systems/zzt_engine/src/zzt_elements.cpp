#include "zzt_elements.hpp"
#include <iostream>

std::vector<zzt_element> zzt_elements (0);

void init_zzt_elements()
{
    zzt_elements.resize(0x36);

    zzt_elements[0x00].name = "Empty";
    zzt_elements[0x00].character = 0x20;
    zzt_elements[0x00].col = 0;
    zzt_elements[0x00].row = 2;

    zzt_elements[0x01].name = "Board edge";
    zzt_elements[0x01].character = 0x20;
    zzt_elements[0x01].col = 0;
    zzt_elements[0x01].row = 2;

    zzt_elements[0x04].name = "Player";
    zzt_elements[0x04].character = 0x02;
    zzt_elements[0x04].col = 2;
    zzt_elements[0x04].row = 0;

    zzt_elements[0x05].name = "Ammo";
    zzt_elements[0x05].character = 0x84;
    zzt_elements[0x05].col = 4;
    zzt_elements[0x05].row = 8;

    zzt_elements[0x06].name = "Torch";
    zzt_elements[0x06].character = 0x9D;
    zzt_elements[0x06].col = 13;
    zzt_elements[0x06].row = 9;

    zzt_elements[0x07].name = "Gem";
    zzt_elements[0x07].character = 0x04;
    zzt_elements[0x07].col = 4;
    zzt_elements[0x07].row = 0;

    zzt_elements[0x08].name = "Key";
    zzt_elements[0x08].character = 0x0C;
    zzt_elements[0x08].col = 12;
    zzt_elements[0x08].row = 0;

    zzt_elements[0x09].name = "Door";
    zzt_elements[0x09].character = 0x0A;
    zzt_elements[0x09].col = 10;
    zzt_elements[0x09].row = 0;

    zzt_elements[0x0A].name = "Scroll";
    zzt_elements[0x0A].character = 0xE8;
    zzt_elements[0x0A].col = 8;
    zzt_elements[0x0A].row = 14;

    zzt_elements[0x0B].name = "Passage";
    zzt_elements[0x0B].character = 0xF0;
    zzt_elements[0x0B].col = 0;
    zzt_elements[0x0B].row = 15;

    zzt_elements[0x0C].name = "Duplicator";
    zzt_elements[0x0C].character = 0xFA;
    zzt_elements[0x0C].col = 10;
    zzt_elements[0x0C].row = 15;

    zzt_elements[0x0D].name = "Bomb";
    zzt_elements[0x0D].character = 0x0B;
    zzt_elements[0x0D].col = 11;
    zzt_elements[0x0D].row = 0;

    zzt_elements[0x0E].name = "Energizer";
    zzt_elements[0x0E].character = 0x7F;
    zzt_elements[0x0E].col = 15;
    zzt_elements[0x0E].row = 7;

    zzt_elements[0x10].name = "Clockwise";
    zzt_elements[0x10].character = 0x2F;
    zzt_elements[0x10].col = 15;
    zzt_elements[0x10].row = 2;

    zzt_elements[0x11].name = "Counter";
    zzt_elements[0x11].character = 0x5C;
    zzt_elements[0x11].col = 12;
    zzt_elements[0x11].row = 5;

    zzt_elements[0x13].name = "Lava";
    zzt_elements[0x13].character = 0x6F;
    zzt_elements[0x13].col = 15;
    zzt_elements[0x13].row = 6;

    zzt_elements[0x14].name = "Forest";
    zzt_elements[0x14].character = 0xB0;
    zzt_elements[0x14].col = 0;
    zzt_elements[0x14].row = 11;

    zzt_elements[0x15].name = "Solid";
    zzt_elements[0x15].character = 0xDB;
    zzt_elements[0x15].col = 11;
    zzt_elements[0x15].row = 13;

    zzt_elements[0x16].name = "Normal";
    zzt_elements[0x16].character = 0xB2;
    zzt_elements[0x16].col = 2;
    zzt_elements[0x16].row = 11;

    zzt_elements[0x17].name = "Breakable";
    zzt_elements[0x17].character = 0xB1;
    zzt_elements[0x17].col = 1;
    zzt_elements[0x17].row = 11;

    zzt_elements[0x18].name = "Boulder";
    zzt_elements[0x18].character = 0xFE;
    zzt_elements[0x18].col = 14;
    zzt_elements[0x18].row = 15;

    zzt_elements[0x19].name = "Slider NS";
    zzt_elements[0x19].character = 0x12;
    zzt_elements[0x19].col = 2;
    zzt_elements[0x19].row = 1;

    zzt_elements[0x1A].name = "Slider EW";
    zzt_elements[0x1A].character = 0x1D;
    zzt_elements[0x1A].col = 13;
    zzt_elements[0x1A].row = 1;

    zzt_elements[0x1B].name = "Fake";
    zzt_elements[0x1B].character = 0xB2;
    zzt_elements[0x1B].col = 2;
    zzt_elements[0x1B].row = 11;

    zzt_elements[0x1C].name = "Invisible";
    zzt_elements[0x1C].character = 0x20;
    zzt_elements[0x1C].col = 0;
    zzt_elements[0x1C].row = 2;

    zzt_elements[0x1D].name = "Blink wall";
    zzt_elements[0x1D].character = 0xCE;
    zzt_elements[0x1D].col = 14;
    zzt_elements[0x1D].row = 12;

    zzt_elements[0x1E].name = "Transporter";
    zzt_elements[0x1E].character = 0xC5;
    zzt_elements[0x1E].col = 5;
    zzt_elements[0x1E].row = 12;

    zzt_elements[0x1F].name = "Line";
    zzt_elements[0x1F].character = 0xCE;
    zzt_elements[0x1F].col = 14;
    zzt_elements[0x1F].row = 12;

    zzt_elements[0x20].name = "Ricochet";
    zzt_elements[0x20].character = 0x2A;
    zzt_elements[0x20].col = 10;
    zzt_elements[0x20].row = 2;

    zzt_elements[0x22].name = "Bear";
    zzt_elements[0x22].character = 0x99;
    zzt_elements[0x22].col = 9;
    zzt_elements[0x22].row = 9;

    zzt_elements[0x23].name = "Ruffian";
    zzt_elements[0x23].character = 0x05;
    zzt_elements[0x23].col = 5;
    zzt_elements[0x23].row = 0;

    zzt_elements[0x24].name = "Object";
    zzt_elements[0x24].character = 0x02;
    zzt_elements[0x24].col = 2;
    zzt_elements[0x24].row = 0;

    zzt_elements[0x25].name = "Slime";
    zzt_elements[0x25].character = 0x2A;
    zzt_elements[0x25].col = 10;
    zzt_elements[0x25].row = 2;

    zzt_elements[0x26].name = "Shark";
    zzt_elements[0x26].character = 0x5E;
    zzt_elements[0x26].col = 14;
    zzt_elements[0x26].row = 5;

    zzt_elements[0x27].name = "Spinning gun";
    zzt_elements[0x27].character = 0x18;
    zzt_elements[0x27].col = 8;
    zzt_elements[0x27].row = 1;

    zzt_elements[0x28].name = "Pusher";
    zzt_elements[0x28].character = 0x10;

    zzt_elements[0x29].name = "Lion";
    zzt_elements[0x29].character = 0xEA;
    zzt_elements[0x29].col = 10;
    zzt_elements[0x29].row = 14;

    zzt_elements[0x2A].name = "Tiger";
    zzt_elements[0x2A].character = 0xE3;
    zzt_elements[0x2A].col = 3;
    zzt_elements[0x2A].row = 14;

    zzt_elements[0x2B].name = "Blink Ray (Vertical)";
    zzt_elements[0x2B].character = 0xBA;

    zzt_elements[0x2C].name = "Head";
    zzt_elements[0x2C].character = 0xE9;
    zzt_elements[0x2C].col = 9;
    zzt_elements[0x2C].row = 14;

    zzt_elements[0x2D].name = "Segment";
    zzt_elements[0x2D].character = 0x4F;
    zzt_elements[0x2D].col = 15;
    zzt_elements[0x2D].row = 4;

    zzt_elements[0x2E].name = " ";
    zzt_elements[0x2E].character = 0x20;
    zzt_elements[0x2E].col = 0;
    zzt_elements[0x2E].row = 2;

    zzt_elements[0x2F].name = "Text (Blue)";
    zzt_elements[0x2F].character = 0x20;
    zzt_elements[0x2F].col = 0;
    zzt_elements[0x2F].row = 2;

    zzt_elements[0x30].name = "Text (Green)";
    zzt_elements[0x30].character = 0x20;
    zzt_elements[0x30].col = 0;
    zzt_elements[0x30].row = 2;

    zzt_elements[0x31].name = "Text (Cyan)";
    zzt_elements[0x31].character = 0x20;
    zzt_elements[0x31].col = 0;
    zzt_elements[0x31].row = 2;

    zzt_elements[0x32].name = "Text (Purple)";
    zzt_elements[0x32].character = 0x20;
    zzt_elements[0x32].col = 0;
    zzt_elements[0x32].row = 2;

    zzt_elements[0x33].name = "Text (Purple)";
    zzt_elements[0x33].character = 0x20;
    zzt_elements[0x33].col = 0;
    zzt_elements[0x33].row = 2;

    zzt_elements[0x34].name = "Text (Brown)";
    zzt_elements[0x34].character = 0x20;
    zzt_elements[0x34].col = 0;
    zzt_elements[0x34].row = 2;

    zzt_elements[0x35].name = "Text (Black)";
    zzt_elements[0x35].character = 0x20;
    zzt_elements[0x35].col = 0;
    zzt_elements[0x35].row = 2;
}

