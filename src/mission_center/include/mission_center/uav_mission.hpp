#include <boost/algorithm/algorithm.hpp>
#include <boost/algorithm/string.hpp>
#include <functional>
#include <keyboard_msgs/msg/key.hpp>
#include <mavros_msgs/msg/state.hpp>
#include <mavros_msgs/srv/command_bool.hpp>
#include <mavros_msgs/srv/command_tol.hpp>
#include <mavros_msgs/srv/set_mode.hpp>
#include <rclcpp/node.hpp>
#include <rclcpp/rclcpp.hpp>
#include <string>
#include <yaml-cpp/yaml.h>

using command_func = std::function<void()>; // Define function pointer type

class UAV_Mission {
public:
  void arm_throttle();
  void takeoff();
  void land();
  void switch_mode(std::string mode);
  template <class T>
  bool getTopicVal(T &returnVal, const std::string &topicName,
                   std::chrono::seconds retryTimeout);
  bool load_mission(const std::string &file_path) {
    YAML::Node config = YAML::LoadFile(file_path);

    if (!config["mission"]) {
      std::cerr << "Error: No 'mission' key found in YAML file.\n";
      return false;
    }

    for (const auto &command : config["mission"]) {
      std::string cmd_name = command["command"].as<std::string>();

      // Check if the command exists in the map
      auto func = uav_funcs.find(cmd_name);
      if (func != uav_funcs.end()) {
        std::cout << "Command: " << cmd_name << std::endl;
        uav_funcs[cmd_name]();
      } else {
        // If command is invalid, throw an exception
        throw std::invalid_argument("Invalid command in .yaml file: " +
                                    cmd_name);
      }

      // Handle additional parameters (altitude, x, y, z, etc.)
      if (command["altitude"])
        std::cout << ", Altitude: " << command["altitude"].as<double>();
      if (command["x"])
        std::cout << ", X: " << command["x"].as<double>();
      if (command["y"])
        std::cout << ", Y: " << command["y"].as<double>();
      if (command["z"])
        std::cout << ", Z: " << command["z"].as<double>();

      std::cout << std::endl;
    }
    return true;
  }

  rclcpp::Node::SharedPtr nh;
  bool init();
  UAV_Mission(rclcpp::Node::SharedPtr node);

private:
  std::unordered_map<std::string, command_func> uav_funcs;
};
