#pragma once

#include <libecs-cpp/ecs.hpp>

class input : public ecs::Component
{
  public:
    input(); 
    input(Json::Value);
    Json::Value Export();
    std::string event;
    Json::Value content;
};
