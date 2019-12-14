#pragma once

#include <libecs-cpp/ecs.hpp>

class song : public ecs::Component
{
  public:
    song(); 
    song(Json::Value);
    Json::Value Export();
    std::string resource_pak;
    std::string name;
    std::string status;
};
