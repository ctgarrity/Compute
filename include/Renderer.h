#pragma once
#include "VkBootstrap.h"

class Renderer {
public:
  void init();
  void destroy();

private:
  vkb::Instance m_instance;

  void create_instance();
};
