#pragma once

#include <libecs-cpp/ecs.hpp>

class SKELETON : public ecs::Component
{
  public:
    SKELETON(); 
    SKELETON(ConfigMap);
    std::string data;
};
