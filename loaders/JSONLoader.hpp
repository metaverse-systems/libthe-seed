#pragma once

#include <string>
#include <libecs-cpp/ecs.hpp>

namespace JSONLoader
{
    class Loader
    {
      public:
        Loader(std::string data);
        ~Loader();
      private:
        Json::Value scene;
    };
}
