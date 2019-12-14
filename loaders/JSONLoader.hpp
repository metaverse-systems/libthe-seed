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

    void StringParse(ecs::Container *, std::string data);
    void FileParse(ecs::Container *, std::string filename);
}
