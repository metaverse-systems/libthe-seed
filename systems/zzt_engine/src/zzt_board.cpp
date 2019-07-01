#include "zzt_board.hpp"
#include "zzt_engine.hpp"

Board::Board(uint8_t *data, zzt_engine *engine)
{
    this->engine = engine;
    this->data = data;
    this->header = (zzt_board_header *)this->data;

    this->board_elements = this->ExpandRLE();
    this->DumpHeader();
    std::cout << "RLE size: " << this->rle_size << std::endl;
    this->board_properties = *(zzt_board_properties *)(this->data + this->rle_size + 0x35);
    this->DumpProperties();
}

void Board::DumpHeader()
{
    std::cout << "Board size: " << this->header->BoardSize << " bytes" << std::endl;
        
    this->name = std::string(this->header->BoardName).substr(0, this->header->BoardNameLength);
    std::cout << "Board name: " << this->name << std::endl;
}

std::vector<uint16_t> Board::ExpandRLE()
{
    std::vector<uint16_t> elements;
    int16_t tiles = 0;
    zzt_rle *rle = (zzt_rle *)(this->data + 0x35);
    ecs::Entity *e = nullptr;

    this->rle_size = 0;
    uint8_t x = 0, y = 0;
    while(tiles < 1500)
    {
        uint8_t counter = 0;
        while(counter < rle->Count)
        {
            elements.push_back((rle->Color << 8) | rle->Element);

            e = this->engine->create_entity();
            this->engine->create_position(e, x, y);
            this->engine->create_zzt_texture(e, rle->Element, rle->Color);
            counter++; tiles++;
            x++;
            if(x > 59)
            {
                x = 0;
                y++;
            }
        }
        rle++;
        this->rle_size += 3;
    }
    return elements;
}

void Board::DumpProperties()
{
    std::cout << "MaxPlayerShots: " << static_cast<unsigned int>(this->board_properties.MaxPlayerShots) << std::endl;
    std::cout << "IsDark: " << (this->board_properties.IsDark ? "Yes" : "No") << std::endl;
    std::cout << "North board: " << static_cast<unsigned int>(this->board_properties.ExitNorth) << std::endl;
    std::cout << "South board: " << static_cast<unsigned int>(this->board_properties.ExitSouth) << std::endl;
    std::cout << "West board: " << static_cast<unsigned int>(this->board_properties.ExitWest) << std::endl;
    std::cout << "East board: " << static_cast<unsigned int>(this->board_properties.ExitEast) << std::endl;
    std::cout << "RestartOnZap: " << (this->board_properties.RestartOnZap ? "Yes" : "No") << std::endl;
    std::cout << "MessageLength: " << static_cast<unsigned int>(this->board_properties.MessageLength) << std::endl;
    std::cout << "Message: " << this->board_properties.Message << std::endl;
    std::cout << "PlayerEnterX: " << static_cast<unsigned int>(this->board_properties.PlayerEnterX) << std::endl;
    std::cout << "PlayerEnterY: " << static_cast<unsigned int>(this->board_properties.PlayerEnterY) << std::endl;
    std::cout << "TimeLimit: " << static_cast<unsigned int>(this->board_properties.TimeLimit) << " seconds" << std::endl;
    std::cout << "StatElementCount: " << static_cast<unsigned int>(this->board_properties.StatElementCount) << std::endl;

    ecs::Entity *e = nullptr;

    e = this->engine->create_entity();
    this->engine->create_position(e, this->board_properties.PlayerEnterX, this->board_properties.PlayerEnterY);
    this->engine->create_zzt_texture(e, 4, 15);
#if 0
    uint8_t *raw = (uint8_t *)(&this->board_properties + sizeof(zzt_board_properties));
    StatusElement *StatElement = nullptr;
    uint16_t increment = 0;
    for(uint8_t i = 0; i <= this->board_properties.StatElementCount; i++)
    {
        StatElement = (StatusElement *)raw;

        std::cout << "LocationX: " << static_cast<unsigned int>(StatElement->LocationX) << std::endl;
        std::cout << "LocationY: " << static_cast<unsigned int>(StatElement->LocationY) << std::endl;
        std::cout << "StepX: " << static_cast<unsigned int>(StatElement->StepX) << std::endl;
        std::cout << "StepY: " << static_cast<unsigned int>(StatElement->StepY) << std::endl;
        std::cout << "Cycle: " << static_cast<unsigned int>(StatElement->Cycle) << std::endl;

        std::cout << "Length: " << static_cast<unsigned int>(StatElement->Length) << std::endl;

        if(StatElement->Length > 0)
        {
            char *code = (char *)(StatElement + sizeof(StatusElement));
            char *temp = new char[StatElement->Length];
            memset(temp, 0, StatElement->Length);
            strncpy(temp, code, StatElement->Length);
            for(uint16_t j = 0; j < StatElement->Length; j++)
            {
                std::string message;
                message += code[j];
                std::cout << message << std::endl;
            }

            std::cout << temp << std::endl;
            increment = sizeof(StatusElement) + StatElement->Length;
        }
        else increment = sizeof(StatusElement) + 8;
        std::cout << "Increment: " << increment << std::endl;
        raw += increment;
    }
#endif
}
