#pragma once

#include <string>
#include <libecs-cpp/ecs.hpp>

namespace JSONLoader
{
    class Loader
    {
      public:
        Loader(ecs::Container *, std::string data);
        ~Loader();
        void Parse();
      private:
        Json::Value scene;
        ecs::Container *container = nullptr;
    };

    void Parse(ecs:: Container *, std::string data);
    void Parse(ecs:: Container *, std::string data, Json::Value config);
}
