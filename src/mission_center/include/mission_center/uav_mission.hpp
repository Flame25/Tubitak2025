#include <boost/algorithm/algorithm.hpp>
#include <boost/algorithm/string.hpp>
#include <functional>
#include <keyboard_msgs/msg/key.hpp>
#include <px4_msgs/msg/detail/trajectory_setpoint__struct.hpp>
#include <px4_msgs/msg/detail/vehicle_command__struct.hpp>
#include <px4_msgs/msg/detail/vehicle_local_position__struct.hpp>
#include <px4_msgs/msg/offboard_control_mode.hpp>
#include <px4_msgs/msg/sensor_gps.hpp>
#include <px4_msgs/msg/trajectory_setpoint.hpp>
#include <px4_msgs/msg/vehicle_command.hpp>
#include <px4_msgs/msg/vehicle_control_mode.hpp>
#include <px4_msgs/msg/vehicle_local_position.hpp>
#include <px4_msgs/msg/vehicle_status.hpp>
#include <rclcpp/logging.hpp>
#include <rclcpp/node.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp/subscription.hpp>
#include <rclcpp/timer.hpp>
#include <std_msgs/msg/float64.hpp>
#include <std_srvs/srv/empty.hpp>
#include <string>
#include <yaml-cpp/yaml.h>

/* Keeping PX4 offboard mode
 * PX4 offboard mode kinda different from guided
 * mode in the ardupilot. We need to keep sending
 * both trajectory and offboard control <= 2hz so
 * we need to spam it even when it's not moving at all.
 */

class UAV_Mission {
public:
  // UAV Commands
  void arm_throttle();
  void takeoff(float height);
  void takeoff2(float height);
  void land();
  void switch_mode(std::string mode);
  void switch_offboard_mode();
  void move(float x, float y, float z);

  // Keep Offboard Mode
  void send_offboard_control();
  void set_trajectory(double x, double y, double z);
  void offboard_loop();

  // Timer Updater
  void timer_callback();

  // Sub Callback
  void
  local_pos_callback(const px4_msgs::msg::VehicleLocalPosition::SharedPtr msg);

  // Utilities

  /**
   * @brief Publish vehicle commands
   * (https://github.com/PX4/PX4-Autopilot/blob/main/msg/VehicleCommand.msg)
   * @param command   Command code (matches VehicleCommand and MAVLink MAV_CMD
   * codes)
   * @param param1    Parameter 1, as defined by MAVLink uint16 VEHICLE_CMD
   * enum.
   * @param param2    Parameter 2, as defined by MAVLink uint16 VEHICLE_CMD
   * enum.
   * @param param3    Parameter 3, as defined by MAVLink uint16 VEHICLE_CMD
   * enum.
   * @param param4    Parameter 4, as defined by MAVLink uint16 VEHICLE_CMD
   * enum.
   * @param param5    Parameter 5, as defined by MAVLink uint16 VEHICLE_CMD
   * enum.
   * @param param6    Parameter 6, as defined by MAVLink uint16 VEHICLE_CMD
   * enum.
   * @param param7    Parameter 7, as defined by MAVLink uint16 VEHICLE_CMD
   * enum.
   */
  void publish_vehicle_command(uint16_t command, float param1 = 0.0,
                               float param2 = 0.0, float param3 = 0.0,
                               float param4 = 0.0, float param5 = 0.0,
                               float param6 = 0.0, float param7 = 0.0);

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
  ~UAV_Mission();

private:
  std::atomic<bool> restart_mission{false};
  std::map<std::string, std::function<void(const YAML::Node &)>> uav_funcs;
  rclcpp::Service<std_srvs::srv::Empty>::SharedPtr service_;
  rclcpp::Service<std_srvs::srv::Empty>::SharedPtr shutdown_service;
  rclcpp::Publisher<px4_msgs::msg::OffboardControlMode>::SharedPtr offboard_pub;
  rclcpp::Publisher<px4_msgs::msg::TrajectorySetpoint>::SharedPtr traj_pub;
  rclcpp::Publisher<px4_msgs::msg::VehicleCommand>::SharedPtr
      vehicle_command_pub;
  px4_msgs::msg::OffboardControlMode offboard_ctrl_msg;
  px4_msgs::msg::TrajectorySetpoint traj_msg;
  std::thread offboard_thread;
  rclcpp::Subscription<px4_msgs::msg::VehicleLocalPosition>::SharedPtr
      local_pos_sub;
  rclcpp::TimerBase::SharedPtr timer;

  float curr_x = 0;
  float curr_y = 0;
  float curr_z = 0;

  bool killPilotCb(const std::shared_ptr<std_srvs::srv::Empty::Request> request,
                   std::shared_ptr<std_srvs::srv::Empty::Response> response) {
    RCLCPP_WARN(nh->get_logger(), "--- Pilot Node Die ---");

    // Now launch shutdown in another thread
    std::thread([]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(
          100)); // short sleep to allow response to go out
      rclcpp::shutdown();
    }).detach();
    return true;
  }
};
