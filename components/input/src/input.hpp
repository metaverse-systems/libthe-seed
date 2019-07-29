#pragma once

#include <libecs-cpp/ecs.hpp>

class input : public ecs::Component
{
  public:
    input(); 
    input(Json::Value);
    Json::Value save();
    std::string action;
    Json::Value content;
};
