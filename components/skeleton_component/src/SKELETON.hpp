#pragma once

#include <libecs-cpp/ecs.hpp>

class SKELETON : public ecs::Component
{
  public:
    SKELETON(); 
    SKELETON(Json::Value);
    Json::Value save();
    std::string data;
};
