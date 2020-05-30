#include "NameParser.hpp"
#include <vector>

NameParser::NameParser(std::string src)
{
    if(src.find("/") != std::string::npos)
    {
        std::vector<std::string> dest;
        std::string temp;
        for(size_t counter = 0; counter < src.size(); counter++)
        {
            if(src[counter] == '/')
            {
                dest.push_back(temp);
                temp = "";
                continue;
            }

            temp += src[counter];
        }

        dest.push_back(temp);

        this->org = dest[0];
        this->library = dest[1];
        return;
    }

    this->library = src;
}
