#include "mission_center/uav_mission.hpp"
#include "keyboard_msgs/msg/detail/key__struct.hpp"
#include <boost/function.hpp>
#include <boost/smart_ptr/shared_ptr.hpp>
#include <chrono>
#include <iostream>
#include <keyboard_msgs/msg/key.hpp>
#include <mavros_msgs/msg/detail/command_code__struct.hpp>
#include <mavros_msgs/msg/detail/state__struct.hpp>
#include <mavros_msgs/srv/detail/command_bool__struct.hpp>
#include <mavros_msgs/srv/detail/set_mode__struct.hpp>
#include <rclcpp/client.hpp>
#include <rclcpp/duration.hpp>
#include <rclcpp/executors.hpp>
#include <rclcpp/future_return_code.hpp>
#include <rclcpp/logging.hpp>
#include <rclcpp/rate.hpp>
#include <rclcpp/utilities.hpp>
#include <rmw/types.h>
#include <sensor_msgs/msg/detail/nav_sat_fix__struct.hpp>
#include <std_msgs/msg/detail/float64__struct.hpp>

// TODO: Landing
// TODO: Take off
// TODO: GPS Lock Checker & stuffs
// TODO: Improc Chaser

UAV_Mission::UAV_Mission(rclcpp::Node::SharedPtr node) {
  nh = node;
  uav_funcs["takeoff"] = [this](const YAML::Node &cmd) {
    double alt = cmd["altitude"] ? cmd["altitude"].as<double>() : 1.0;
    this->takeoff(alt);
  };
  uav_funcs["arm_throttle"] = std::bind(&UAV_Mission::arm_throttle, this);
  uav_funcs["init"] = std::bind(&UAV_Mission::init, this);
  uav_funcs["land"] = std::bind(&UAV_Mission::land, this);
}

template <class T>
bool UAV_Mission::getTopicVal(T &returnVal, const std::string &topicName,
                              std::chrono::seconds retryTimeout) {
  RCLCPP_INFO_STREAM(nh->get_logger(),
                     "Waiting for message from " << topicName);

  bool got_msg = false;
  std::shared_ptr<T> last_msg = nullptr;

  // Create a temporary subscription
  auto sub =
      nh->create_subscription<T>(topicName, 10, [&](typename T::SharedPtr msg) {
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

void UAV_Mission::switch_mode(std::string mode) {
  RCLCPP_INFO_STREAM(nh->get_logger(), "--- Changing mode to " << mode);
  rclcpp::Client<mavros_msgs::srv::SetMode>::SharedPtr setMode_client;
  setMode_client =
      nh->create_client<mavros_msgs::srv::SetMode>("/mavros/set_mode");

  auto req = std::make_shared<mavros_msgs::srv::SetMode::Request>();
  req->base_mode = 0;
  req->custom_mode = mode;
  mavros_msgs::msg::State curr_state;

  // The only way to exit this loop with no error is when desired mode is
  // reached
  uint8_t count = 0;
  do {
    count++;
    if (count > 5) {
      RCLCPP_FATAL(nh->get_logger(), "---Mode changing failed. Terminate.---");
      return;
    }

    auto future = setMode_client->async_send_request(req);
    if (rclcpp::spin_until_future_complete(nh, future) ==
        rclcpp::FutureReturnCode::SUCCESS) {
      auto response = future.get();
      if (!response->mode_sent) {
        RCLCPP_WARN(nh->get_logger(),
                    "---Mode changing failed, retrying...---");
      }
      rclcpp::sleep_for(std::chrono::seconds(5));
      int gotTopic =
          getTopicVal(curr_state, "/mavros/state", std::chrono::seconds(5));
    } else {
      RCLCPP_FATAL_STREAM(nh->get_logger(), "Terminate in func " << __func__);
      return;
    }
  } while (boost::algorithm::to_lower_copy(curr_state.mode) !=
           boost::algorithm::to_lower_copy(mode));
}

void UAV_Mission::land() {
  RCLCPP_INFO(nh->get_logger(), "--- Land Initiated ---");
  switch_mode("LAND");
}

void UAV_Mission::arm_throttle() {
  RCLCPP_INFO(nh->get_logger(), "--- Arming ---");
  rclcpp::Client<mavros_msgs::srv::CommandBool>::SharedPtr client;
  client =
      nh->create_client<mavros_msgs::srv::CommandBool>("/mavros/cmd/arming");
  // Create request
  auto request = std::make_shared<mavros_msgs::srv::CommandBool::Request>();
  request->value = true;
  mavros_msgs::msg::State curr_state;
  rclcpp::Duration dur(std::chrono::seconds(5));
  int count = 0;
  int threshold = 5;
  do {
    count++;
    if (count > threshold) {
      RCLCPP_FATAL(nh->get_logger(), "--- Arming Failed ---");
      return;
    }

    auto future = client->async_send_request(request);
    if (rclcpp::spin_until_future_complete(nh, future) ==
        rclcpp::FutureReturnCode::SUCCESS) {
      auto response = future.get();
      if (!response->success) {
        RCLCPP_WARN(nh->get_logger(), "---Arming failed, retrying...---");
      } else {
        RCLCPP_INFO(nh->get_logger(), "---Arming succeeded!---");
      }
      rclcpp::sleep_for(std::chrono::seconds(5));
      int gotTopic =
          getTopicVal(curr_state, "/mavros/state", std::chrono::seconds(3));
    } else {
      RCLCPP_WARN(nh->get_logger(), "---Service call failed---");
    }
  } while (!curr_state.armed);
}
void UAV_Mission::takeoff(double height) {
  rclcpp::Client<mavros_msgs::srv::CommandTOL>::SharedPtr takeoff_client_;
  takeoff_client_ =
      nh->create_client<mavros_msgs::srv::CommandTOL>("/mavros/cmd/takeoff");
  while (!takeoff_client_->wait_for_service(std::chrono::seconds(1))) {
    RCLCPP_WARN(nh->get_logger(), "Waiting for takeoff service...");
  }

  auto request = std::make_shared<mavros_msgs::srv::CommandTOL::Request>();
  request->altitude = height;
  request->min_pitch = 0;
  request->yaw = 0;
  request->latitude = 0;
  request->longitude = 0;
  auto future = takeoff_client_->async_send_request(request);
  auto status =
      rclcpp::spin_until_future_complete(nh, future, std::chrono::seconds(5));
  if (status == rclcpp::FutureReturnCode::SUCCESS) {
    auto res = future.get();
    RCLCPP_INFO(nh->get_logger(),
                "Takeoff service response: success=%d, result=%d", res->success,
                res->result);
  } else if (status == rclcpp::FutureReturnCode::TIMEOUT) {
    RCLCPP_ERROR(nh->get_logger(), "Takeoff service timed out");
  } else {
    RCLCPP_ERROR(nh->get_logger(), "Takeoff service failed");
  }

  // TODO: Wait until desired alt
}
bool UAV_Mission::init() {
  RCLCPP_INFO(nh->get_logger(), "-- Initialize UAV Mission --");
  // TODO: Wait for GPS

  bool GPSFound = false;
  bool Heading = false;
  double currentPoint_lat;
  double currentPoint_lon;
  double currentHeading;

  boost::function<void(const sensor_msgs::msg::NavSatFix &)>
      globalPositionCallback = [&](const sensor_msgs::msg::NavSatFix &msg) {
        currentPoint_lat = msg.latitude;
        currentPoint_lon = msg.longitude;
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

  rmw_qos_profile_t qos = rmw_qos_profile_default;
  qos.reliability = RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT;

  auto qos_profile = rclcpp::QoS(rclcpp::QoSInitialization::from_rmw(qos), qos);
  rclcpp::Subscription<sensor_msgs::msg::NavSatFix>::SharedPtr gps_sub;
  gps_sub = nh->create_subscription<sensor_msgs::msg::NavSatFix>(
      "/mavros/global_position/global", rclcpp::SensorDataQoS(),
      globalPositionCallback);
  rclcpp::Subscription<std_msgs::msg::Float64>::SharedPtr heading_sub;
  heading_sub = nh->create_subscription<std_msgs::msg::Float64>(
      "/mavros/global_position/compass_hdg", rclcpp::SensorDataQoS(),
      headingCallback);

  while (!GPSFound) {
    RCLCPP_WARN(nh->get_logger(), "Waiting for GPS...");
    rclcpp::spin_some(nh);
    rclcpp::sleep_for(std::chrono::milliseconds(100));
  }

  RCLCPP_WARN(nh->get_logger(), "---Press f to start mission---");
  rclcpp::Subscription<keyboard_msgs::msg::Key>::SharedPtr
      sub; // Subscriber pointer
  sub = nh->create_subscription<keyboard_msgs::msg::Key>("/keydown", 10, keyCb);
  while (!startMission) {
    rclcpp::spin_some(nh);
  }
  return true;
}
