#include "JSONLoader.hpp"

namespace JSONLoader
{
    Loader::Loader(std::string data)
    {
        Json::Reader reader;
        if(!reader.parse(data.c_str(), this->scene))
        {
            std::string err = "Couldn't parse scene: " + data;
            throw std::runtime_error(err);
        }
    }
}
