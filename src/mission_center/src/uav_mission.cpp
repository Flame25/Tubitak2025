#include "mission_center/uav_mission.hpp"
#include <boost/function.hpp>
#include <boost/smart_ptr/shared_ptr.hpp>
#include <chrono>
#include <iostream>
#include <keyboard_msgs/msg/key.hpp>
#include <px4_msgs/msg/detail/offboard_control_mode__struct.hpp>
#include <px4_msgs/msg/detail/sensor_gps__struct.hpp>
#include <px4_msgs/msg/detail/vehicle_command__struct.hpp>
#include <px4_msgs/msg/detail/vehicle_control_mode__struct.hpp>
#include <px4_msgs/msg/detail/vehicle_status__struct.hpp>
#include <random>
#include <rclcpp/client.hpp>
#include <rclcpp/create_timer.hpp>
#include <rclcpp/duration.hpp>
#include <rclcpp/executors.hpp>
#include <rclcpp/future_return_code.hpp>
#include <rclcpp/logging.hpp>
#include <rclcpp/qos.hpp>
#include <rclcpp/rate.hpp>
#include <rclcpp/utilities.hpp>
#include <rmw/types.h>
#include <std_msgs/msg/bool.hpp>

// TODO: Improc Chaser

UAV_Mission::UAV_Mission(rclcpp::Node::SharedPtr node) {
  nh = node;
  service_ = nh->create_service<std_srvs::srv::Empty>(
      "/pilot_node/restart_mission",
      std::bind(&UAV_Mission::restartMission, this, std::placeholders::_1,
                std::placeholders::_2));

  shutdown_service = nh->create_service<std_srvs::srv::Empty>(
      "/pilot_node/kill_pilot",
      std::bind(&UAV_Mission::killPilotCb, this, std::placeholders::_1,
                std::placeholders::_2));

  offboard_pub = nh->create_publisher<px4_msgs::msg::OffboardControlMode>(
      "/fmu/in/offboard_control_mode", 10);

  offboard_ctrl_msg.position = true;
  offboard_ctrl_msg.velocity = false;
  offboard_ctrl_msg.acceleration = false;
  offboard_ctrl_msg.attitude = false;
  offboard_ctrl_msg.body_rate = false;
  offboard_ctrl_msg.timestamp = nh->get_clock()->now().nanoseconds() / 1000;

  traj_pub = nh->create_publisher<px4_msgs::msg::TrajectorySetpoint>(
      "/fmu/in/trajectory_setpoint", 10);

  traj_msg.timestamp = nh->get_clock()->now().nanoseconds() / 1000;
  traj_msg.position = {0.0, 0.0, 0.0};
  traj_msg.yaw = 0.0;

  uav_funcs["takeoff"] = [this](const YAML::Node &cmd) {
    float alt = cmd["altitude"] ? cmd["altitude"].as<float>() : 1.0;
    this->takeoff2(alt);
  };

  uav_funcs["switch_mode"] = [this](const YAML::Node &cmd) {
    std::string mode = cmd["mode"] ? cmd["mode"].as<std::string>() : "guided";
    this->switch_mode(mode);
  };

  uav_funcs["arm_throttle"] = std::bind(&UAV_Mission::arm_throttle, this);
  uav_funcs["init"] = std::bind(&UAV_Mission::init, this);
  uav_funcs["land"] = std::bind(&UAV_Mission::land, this);
  uav_funcs["offboard_mode"] =
      std::bind(&UAV_Mission::switch_offboard_mode, this);
  uav_funcs["move"] = [this](const YAML::Node &cmd) {
    float x = cmd["x"] ? cmd["x"].as<float>() : 0.0;
    float y = cmd["y"] ? cmd["y"].as<float>() : 0.0;
    float z = cmd["z"] ? cmd["z"].as<float>() : 0.0;
    this->move(x, y, z);
  };

  // offboard_thread = std::thread(&UAV_Mission::offboard_loop, this);

  // Timer updater
  // timer = rclcpp::create_wall_timer(std::chrono::milliseconds(1000 / 30),
  //                                  std::bind(&UAV_Mission::));

  local_pos_sub = nh->create_subscription<px4_msgs::msg::VehicleLocalPosition>(
      "/fmu/out/vehicle_local_position", rclcpp::SensorDataQoS(),
      std::bind(&UAV_Mission::local_pos_callback, this, std::placeholders::_1));

  vehicle_command_pub = nh->create_publisher<px4_msgs::msg::VehicleCommand>(
      "/fmu/in/vehicle_command", 10);
}

UAV_Mission::~UAV_Mission() {
  if (offboard_thread.joinable())
    offboard_thread.join();
}

void UAV_Mission::local_pos_callback(
    const px4_msgs::msg::VehicleLocalPosition::SharedPtr msg) {
  curr_x = msg->x;
  curr_y = msg->y;
  curr_z = msg->z;
}

bool UAV_Mission::restartMission(
    const std::shared_ptr<std_srvs::srv::Empty::Request> request,
    std::shared_ptr<std_srvs::srv::Empty::Response> response) {
  RCLCPP_WARN(nh->get_logger(), "--- Restarting Mission Pilot Node ---");
  restart_mission.store(true);
  return true;
}

template <class T>
bool UAV_Mission::getTopicVal(T &returnVal, const std::string &topicName,
                              std::chrono::seconds retryTimeout) {
  RCLCPP_INFO_STREAM(nh->get_logger(),
                     "Waiting for message from " << topicName);

  rmw_qos_profile_t qos = rmw_qos_profile_default;
  qos.reliability = RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT;
  bool got_msg = false;
  std::shared_ptr<T> last_msg = nullptr;

  // Create a temporary subscription
  auto sub = nh->create_subscription<T>(topicName, rclcpp::SensorDataQoS(),
                                        [&](typename T::SharedPtr msg) {
                                          last_msg = std::make_shared<T>(*msg);
                                          got_msg = true;
                                        });

  // Timeout logic
  auto start_time = std::chrono::steady_clock::now();
  auto timeout = std::chrono::duration<double>(retryTimeout);

  while (!got_msg &&
         (std::chrono::steady_clock::now() - start_time) < timeout) {
    rclcpp::spin_some(nh);
    rclcpp::sleep_for(std::chrono::milliseconds(100));
  }

  if (!got_msg || !last_msg) {
    RCLCPP_FATAL_STREAM(nh->get_logger(),
                        "Cannot get message from " << topicName);
    return false;
  }

  returnVal = *last_msg;
  return true;
}

void UAV_Mission::offboard_loop() {
  rclcpp::Rate rate(10);
  while (rclcpp::ok()) {
    traj_msg.timestamp = nh->get_clock()->now().nanoseconds() / 1000;
    // traj_pub->publish(traj_msg);
    offboard_ctrl_msg.timestamp = nh->get_clock()->now().nanoseconds() / 1000;
    offboard_pub->publish(offboard_ctrl_msg);

    rate.sleep();
  }
}

void UAV_Mission::switch_offboard_mode() {

  px4_msgs::msg::VehicleCommand cmd{};
  cmd.timestamp = nh->get_clock()->now().nanoseconds() / 1000;
  cmd.command = px4_msgs::msg::VehicleCommand::VEHICLE_CMD_DO_SET_MODE;
  cmd.param1 = 1; // Use custom mode
  cmd.param2 = 6; // PX4_CUSTOM_MAIN_MODE_OFFBOARD
  cmd.target_system = 1;
  cmd.target_component = 1;
  cmd.source_system = 1;
  cmd.source_component = 1;
  cmd.from_external = true;

  // Create Message
  px4_msgs::msg::VehicleCommand msg;
  msg.param1 = 1.0;
  msg.param2 = 0.0;
  msg.command = px4_msgs::msg::VehicleCommand::VEHICLE_CMD_COMPONENT_ARM_DISARM;

  msg.target_system = 1;
  msg.target_component = 1;
  msg.source_system = 1;
  msg.source_component = 1;
  msg.from_external = true;
  msg.timestamp = nh->get_clock()->now().nanoseconds() / 1000;

  px4_msgs::msg::VehicleStatus curr_status;
  do {
    vehicle_command_pub->publish(cmd);
    rclcpp::spin_some(nh);
    if (!getTopicVal(curr_status, "/fmu/out/vehicle_status",
                     std::chrono::seconds(5))) {
      RCLCPP_ERROR(nh->get_logger(),
                   "Failed to get topic /fmu/out/vehicle_status ");
    }
  } while (
      curr_status.nav_state !=
      px4_msgs::msg::VehicleStatus::NAVIGATION_STATE_OFFBOARD); // Offboard Mode
                                                                // (14)
  RCLCPP_INFO(nh->get_logger(), "--- Switched to offboard mode ---");
}

void UAV_Mission::move(float x, float y, float z) {
  RCLCPP_INFO(nh->get_logger(), "=== Moving by %.2f %.2f %.2f ===", x, y, z);
  rclcpp::spin_some(nh);
  traj_msg.position = {curr_x + x, curr_y + y, curr_z + z};

  bool reached_target = false;
  rclcpp::Time start_time = nh->now();

  auto position_cb =
      [&](const px4_msgs::msg::VehicleLocalPosition::SharedPtr msg) {
        float dx = msg->x - x;
        float dy = msg->y - y;
        float dz = msg->z - z;
        float dist = sqrt(dx * dx + dy * dy + dz * dz);

        if (dist < 0.3) {
          reached_target = true;
        }
      };

  auto sub = nh->create_subscription<px4_msgs::msg::VehicleLocalPosition>(
      "/fmu/out/vehicle_local_position", rclcpp::SensorDataQoS(), position_cb);

  while (!reached_target && (nh->now() - start_time).seconds() < 10.0) {
    traj_pub->publish(traj_msg);
    rclcpp::sleep_for(std::chrono::milliseconds(100));
    rclcpp::spin_some(nh);
  }
  rclcpp::sleep_for(std::chrono::seconds(2));
}

void UAV_Mission::switch_mode(std::string mode) {
  // RCLCPP_INFO_STREAM(nh->get_logger(), "--- Changing mode to " << mode);
  // rclcpp::Client<mavros_msgs::srv::SetMode>::SharedPtr setMode_client;
  // setMode_client =
  //     nh->create_client<mavros_msgs::srv::SetMode>("/mavros/set_mode");

  // auto req = std::make_shared<mavros_msgs::srv::SetMode::Request>();
  // req->base_mode = 0;
  // req->custom_mode = mode;
  // mavros_msgs::msg::State curr_state;

  // The only way to exit this loop with no error is when desired mode is
  // reached
  uint8_t count = 0;
  // do {
  //   count++;
  //   if (count > 5) {
  //     RCLCPP_FATAL(nh->get_logger(), "---Mode changing failed.
  //     Terminate.---"); return;
  //   }

  //  auto future = setMode_client->async_send_request(req);
  //  if (rclcpp::spin_until_future_complete(nh, future) ==
  //      rclcpp::FutureReturnCode::SUCCESS) {
  //    auto response = future.get();
  //    if (!response->mode_sent) {
  //      RCLCPP_WARN(nh->get_logger(),
  //                  "---Mode changing failed, retrying...---");
  //    }
  //    rclcpp::sleep_for(std::chrono::seconds(5));
  //    int gotTopic =
  //        getTopicVal(curr_state, "/mavros/state", std::chrono::seconds(5));
  //  } else {
  //    RCLCPP_FATAL_STREAM(nh->get_logger(), "Terminate in func " <<
  //    __func__); return;
  //  }
  //} while (boost::algorithm::to_lower_copy(curr_state.mode) !=
  //         boost::algorithm::to_lower_copy(mode));
}

void UAV_Mission::land() {
  RCLCPP_INFO(nh->get_logger(), "--- Land Initiated ---");
  publish_vehicle_command(px4_msgs::msg::VehicleCommand::VEHICLE_CMD_NAV_LAND);
  RCLCPP_INFO(nh->get_logger(), "Land command send");
}

void UAV_Mission::publish_vehicle_command(uint16_t command, float param1,
                                          float param2, float param3,
                                          float param4, float param5,
                                          float param6, float param7) {
  px4_msgs::msg::VehicleCommand msg{};
  msg.param1 = param1;
  msg.param2 = param2;
  msg.param3 = param3;
  msg.param4 = param4;
  msg.param5 = param5;
  msg.param6 = param6;
  msg.param7 = param7;
  msg.command = command;
  msg.target_system = 1;
  msg.target_component = 1;
  msg.source_system = 1;
  msg.source_component = 1;
  msg.from_external = true;
  msg.timestamp = nh->get_clock()->now().nanoseconds() / 1000;
  vehicle_command_pub->publish(msg);
}

void UAV_Mission::arm_throttle() {
  RCLCPP_INFO(nh->get_logger(), "--- Arming ---");

  // Create Message
  px4_msgs::msg::VehicleCommand msg;
  msg.param1 = 1.0;
  msg.param2 = 0.0;
  msg.command = px4_msgs::msg::VehicleCommand::VEHICLE_CMD_COMPONENT_ARM_DISARM;

  msg.target_system = 1;
  msg.target_component = 1;
  msg.source_system = 1;
  msg.source_component = 1;
  msg.from_external = true;
  msg.timestamp = nh->get_clock()->now().nanoseconds() / 1000;

  px4_msgs::msg::VehicleControlMode flags;
  rclcpp::Duration dur(std::chrono::seconds(5));
  int count = 0;
  int threshold = 5;
  do {
    count++;
    if (count > threshold) {
      RCLCPP_FATAL(nh->get_logger(), "--- Arming Failed ---");
      return;
    }

    vehicle_command_pub->publish(msg);
    int gotTopic = getTopicVal(flags, "/fmu/out/vehicle_control_mode",
                               std::chrono::seconds(3));

  } while (!flags.flag_armed);

  RCLCPP_WARN(nh->get_logger(), "--- Armed ---");
  rclcpp::sleep_for(std::chrono::seconds(2));
}

void UAV_Mission::takeoff2(float height) {
  RCLCPP_INFO(nh->get_logger(), "=== Takeoff2 : %.2f meter ===", height);

  px4_msgs::msg::VehicleStatus status;
  int succ =
      getTopicVal(status, "/fmu/out/vehicle_status", std::chrono::seconds(1));

  // Arm command
  px4_msgs::msg::VehicleCommand arm_msg{};
  arm_msg.timestamp = nh->get_clock()->now().nanoseconds() / 1000;
  arm_msg.param1 = 1.0f; // arm
  arm_msg.command =
      px4_msgs::msg::VehicleCommand::VEHICLE_CMD_COMPONENT_ARM_DISARM;
  arm_msg.target_system = 1;
  arm_msg.target_component = 1;
  arm_msg.source_system = 1;
  arm_msg.source_component = 1;
  arm_msg.from_external = true;

  // Takeoff message
  px4_msgs::msg::VehicleCommand takeoff_cmd{};
  takeoff_cmd.param1 = 0.0f;   // takeoff pitch
  takeoff_cmd.param7 = height; // altitude
  takeoff_cmd.command = px4_msgs::msg::VehicleCommand::VEHICLE_CMD_NAV_TAKEOFF;
  takeoff_cmd.target_system = 1;
  takeoff_cmd.target_component = 1;
  takeoff_cmd.source_system = 1;
  takeoff_cmd.source_component = 1;
  takeoff_cmd.from_external = true;

  // Keep sending takeoff command until we enter AUTO_TAKEOFF
  while (rclcpp::ok()) {
    rclcpp::spin_some(nh); // update nav_state from subscriber callback

    // Check nav_state
    if (status.nav_state ==
        px4_msgs::msg::VehicleStatus::NAVIGATION_STATE_AUTO_TAKEOFF) {
      RCLCPP_INFO(nh->get_logger(), "Entered NAVIGATION_STATE_AUTO_TAKEOFF");
      break;
    }

    takeoff_cmd.timestamp = nh->get_clock()->now().nanoseconds() / 1000;
    vehicle_command_pub->publish(takeoff_cmd);
    RCLCPP_INFO(nh->get_logger(), "Sending NAV_TAKEOFF command...");

    rclcpp::sleep_for(std::chrono::milliseconds(200));
  }

  while (rclcpp::ok()) {
    rclcpp::spin_some(nh); // update nav_state from subscriber callback

    // Check nav_state
    if (status.nav_state ==
        px4_msgs::msg::VehicleStatus::NAVIGATION_STATE_AUTO_LOITER) {
      RCLCPP_INFO(nh->get_logger(), "Entered NAVIGATION_STATE_AUTO_LOITER");
      break;
    }

    takeoff_cmd.timestamp = nh->get_clock()->now().nanoseconds() / 1000;
    vehicle_command_pub->publish(arm_msg);
    RCLCPP_INFO(nh->get_logger(), "Sending ARMING command...");

    rclcpp::sleep_for(std::chrono::milliseconds(200));
  }

  RCLCPP_INFO(nh->get_logger(), "Takeoff sequence complete");
}

void UAV_Mission::takeoff(float height) {

  RCLCPP_INFO(nh->get_logger(), "=== Takeoff : %.2f meter ===", height);
  rclcpp::spin_some(nh);
  traj_msg.position = {curr_x, curr_y, curr_z + height};

  bool reached_target = false;
  rclcpp::Time start_time = nh->now();

  auto position_cb =
      [&](const px4_msgs::msg::VehicleLocalPosition::SharedPtr msg) {
        float dx = 0;
        float dy = 0;
        float dz = msg->z - height;
        float dist = sqrt(dx * dx + dy * dy + dz * dz);

        if (dist < 0.1) {
          reached_target = true;
        }
      };

  auto sub = nh->create_subscription<px4_msgs::msg::VehicleLocalPosition>(
      "/fmu/out/vehicle_local_position", rclcpp::SensorDataQoS(), position_cb);

  while (!reached_target && (nh->now() - start_time).seconds() < 10.0) {
    traj_pub->publish(traj_msg);
    rclcpp::sleep_for(std::chrono::milliseconds(100));
    rclcpp::spin_some(nh);
  }
  rclcpp::sleep_for(std::chrono::seconds(2));
}
bool UAV_Mission::init() {
  RCLCPP_INFO(nh->get_logger(), "-- Initialize UAV Mission --");

  // TODO : Fix for heading

  bool GPSFound = false;
  bool Heading = false;
  double currentPoint_lat;
  double currentPoint_lon;
  double currentHeading;

  boost::function<void(const px4_msgs::msg::SensorGps &)>
      globalPositionCallback = [&](const px4_msgs::msg::SensorGps &msg) {
        currentPoint_lat = msg.latitude_deg;
        currentPoint_lon = msg.longitude_deg;
        GPSFound = true;
      };

  boost::function<void(const std_msgs::msg::Float64 &)> headingCallback =
      [&](const std_msgs::msg::Float64 &msg) {
        Heading = true;
        currentHeading = msg.data;
      };

  boost::shared_ptr<const keyboard_msgs::msg::Key> keyPtr;

  // Reading for keyboard to run missions
  bool startMission = false;
  boost::function<void(const keyboard_msgs::msg::Key &)> keyCb =
      [&](const keyboard_msgs::msg::Key &keyPress) {
        if (keyPress.code == keyboard_msgs::msg::Key::KEY_F) {
          std::string topic_name = "/safety_node/active";
          size_t pub_count = nh->count_publishers(topic_name);
          if (pub_count > 0) {
            RCLCPP_WARN(nh->get_logger(), "---Starting mission---");
            startMission = true;
          } else {
            RCLCPP_ERROR(nh->get_logger(), "---Safety Node not running---");
          }
        }
      };

  boost::function<void(const std_msgs::msg::Bool &)> startCb =
      [&](const std_msgs::msg::Bool &msg) {
        if (msg.data) {
          std::string topic_name = "/safety_node/active";
          size_t pub_count = nh->count_publishers(topic_name);
          if (pub_count > 0) {
            RCLCPP_WARN(nh->get_logger(), "---Starting mission---");
            startMission = true;
          } else {
            RCLCPP_ERROR(nh->get_logger(), "---Safety Node not running---");
          }
        }
      };

  rmw_qos_profile_t qos = rmw_qos_profile_default;
  qos.reliability = RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT;

  auto qos_profile = rclcpp::QoS(rclcpp::QoSInitialization::from_rmw(qos), qos);
  rclcpp::Subscription<px4_msgs::msg::SensorGps>::SharedPtr gps_sub;
  gps_sub = nh->create_subscription<px4_msgs::msg::SensorGps>(
      "/fmu/out/vehicle_gps_position", rclcpp::SensorDataQoS(),
      globalPositionCallback);
  // rclcpp::Subscription<std_msgs::msg::Float64>::SharedPtr heading_sub;
  // heading_sub = nh->create_subscription<std_msgs::msg::Float64>(
  //     "/mavros/global_position/compass_hdg", rclcpp::SensorDataQoS(),
  //     headingCallback);

  while (!GPSFound) {
    RCLCPP_WARN(nh->get_logger(), "Waiting for GPS...");
    rclcpp::spin_some(nh);
    if (restart_mission.load()) {
      return false;
    }
    rclcpp::sleep_for(std::chrono::milliseconds(100));
  }

  RCLCPP_WARN(nh->get_logger(), "---Mission ready to start mission---");
  rclcpp::Subscription<keyboard_msgs::msg::Key>::SharedPtr
      sub; // Subscriber pointer
  sub = nh->create_subscription<keyboard_msgs::msg::Key>("/keydown", 10, keyCb);

  rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr start_pub;
  start_pub = nh->create_subscription<std_msgs::msg::Bool>(
      "mission_center/start_mission", 10, startCb);
  while (!startMission) {
    rclcpp::spin_some(nh);
  }
  return true;
}
