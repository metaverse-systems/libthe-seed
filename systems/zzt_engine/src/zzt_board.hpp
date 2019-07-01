#pragma once
#include <string>
#include <vector>
#include <iostream>

struct zzt_bh_t
{
    int16_t BoardSize;
    uint8_t BoardNameLength;
    char BoardName[50];
} __attribute__((__packed__));

typedef struct zzt_bh_t zzt_board_header;

struct zzt_rle_t
{
    uint8_t Count;
    uint8_t Element;
    uint8_t Color;
} __attribute__((__packed__));

typedef struct zzt_rle_t zzt_rle;

struct zzt_bp_t
{
    uint8_t MaxPlayerShots;
    uint8_t IsDark;
    uint8_t ExitNorth;
    uint8_t ExitSouth;
    uint8_t ExitWest;
    uint8_t ExitEast;
    uint8_t RestartOnZap;
    uint8_t MessageLength;
    char    Message[58];
    uint8_t PlayerEnterX;
    uint8_t PlayerEnterY;
    int16_t TimeLimit;
    uint8_t unused[16];
    int16_t StatElementCount;
} __attribute__((__packed__));

typedef struct zzt_bp_t zzt_board_properties;

class zzt_engine;

class Board
{
  public:
    Board(uint8_t *, zzt_engine *);
    void DumpHeader();
    void DumpRLE();
    void DumpProperties();
    std::vector<uint16_t> ExpandRLE();
  private:
    uint8_t *data;
    zzt_board_header *header;
    std::string name;
    uint16_t rle_size = 0;
    std::vector<uint16_t> board_elements;
    zzt_board_properties board_properties;
    zzt_engine *engine = nullptr;
};
