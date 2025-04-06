#include "mission_center/uav_mission.hpp"
#include <fstream>
#include <iostream>
#include <yaml-cpp/yaml.h>

int main() {
  UAV_Mission new_m;
  std::string filename = "mission.yaml";
  new_m.load_mission(filename);
  return 0;
}
