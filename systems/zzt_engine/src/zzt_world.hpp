#pragma once
#include <string>
#include <vector>

struct zzt_wh 
{
    int16_t WorldType;
    int16_t NumBoards;
    int16_t PlayerAmmo;
    int16_t PlayerGems;
    char PlayerKeys[7];
    int16_t PlayerHealth;
    int16_t PlayerBoard;

    int16_t PlayerTorches;
    int16_t TorchCycles;
    int16_t EnergyCycles;
    int16_t unused_1;
    int16_t PlayerScore;
    uint8_t WorldNameLength;
    char WorldName[20];

    uint8_t Flag0Length;
    char Flag0[20];
    uint8_t Flag1Length;
    char Flag1[20];
    uint8_t Flag2Length;
    char Flag2[20];
    uint8_t Flag3Length;
    char Flag3[20];
    uint8_t Flag4Length;
    char Flag4[20];
    uint8_t Flag5Length;
    char Flag5[20];
    uint8_t Flag6Length;
    char Flag6[20];
    uint8_t Flag7Length;
    char Flag7[20];
    uint8_t Flag8Length;
    char Flag8[20];
    uint8_t Flag9Length;
    char Flag9[20];
    uint8_t FlagXLength;
    char FlagX[20];

    int16_t TimePassed;
    int16_t TimePassedTicks;
    uint8_t Locked;
    uint8_t unused_2[14];

} __attribute__((__packed__));

typedef struct zzt_wh zzt_world_header;

class zzt_engine;

class World
{
  public:
    World(std::string filename);
    ~World();
    void DumpHeader();
    uint8_t *GetBoard(int16_t board);
    zzt_world_header *header = nullptr;
  private:
    std::string filename;
    uint8_t *data = nullptr;
    std::vector<std::string> flags;
};

