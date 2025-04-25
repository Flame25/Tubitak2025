#include <boost/algorithm/algorithm.hpp>
#include <boost/algorithm/string.hpp>
#include <functional>
#include <keyboard_msgs/msg/key.hpp>
#include <mavros_msgs/msg/state.hpp>
#include <mavros_msgs/srv/command_bool.hpp>
#include <mavros_msgs/srv/command_tol.hpp>
#include <mavros_msgs/srv/set_mode.hpp>
#include <rclcpp/logging.hpp>
#include <rclcpp/node.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/nav_sat_fix.hpp>
#include <std_msgs/msg/float64.hpp>
#include <std_srvs/srv/empty.hpp>
#include <string>
#include <yaml-cpp/yaml.h>

class UAV_Mission {
public:
  void arm_throttle();
  void takeoff(double height);
  void land();
  void switch_mode(std::string mode);
  bool
  restartMission(const std::shared_ptr<std_srvs::srv::Empty::Request> request,
                 std::shared_ptr<std_srvs::srv::Empty::Response> response);

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

      if (restart_mission.load()) {
        RCLCPP_INFO(nh->get_logger(), "Restarting Mission");
        restart_mission.store(false);
        return false;
      }
      // Check if the command exists in the map
      auto func = uav_funcs.find(cmd_name);
      if (func != uav_funcs.end()) {
        std::cout << "Command: " << cmd_name << std::endl;
        func->second(command);
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

    RCLCPP_INFO(nh->get_logger(), "Mission Complete!");
    this->load_mission(file_path);
    return true;
  }

  rclcpp::Node::SharedPtr nh;
  bool init();
  UAV_Mission(rclcpp::Node::SharedPtr node);

private:
  std::atomic<bool> restart_mission{false};
  std::map<std::string, std::function<void(const YAML::Node &)>> uav_funcs;
  rclcpp::Service<std_srvs::srv::Empty>::SharedPtr service_;
  rclcpp::Service<std_srvs::srv::Empty>::SharedPtr shutdown_service;

  bool killPilotCb(const std::shared_ptr<std_srvs::srv::Empty::Request> request,
                   std::shared_ptr<std_srvs::srv::Empty::Response> response) {
    RCLCPP_WARN(nh->get_logger(), "--- Pilot Node Die ---");
    rclcpp::shutdown();
    return true;
  }
};
