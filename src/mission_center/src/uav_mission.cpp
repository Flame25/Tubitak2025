#include "mission_center/uav_mission.hpp"
#include <boost/function.hpp>
#include <boost/smart_ptr/shared_ptr.hpp>
#include <chrono>
#include <geometry_msgs/msg/detail/pose__struct.hpp>
#include <geometry_msgs/msg/detail/pose_stamped__struct.hpp>
#include <geometry_msgs/msg/pose_stamped.hpp>
#include <iostream>
#include <keyboard_msgs/msg/key.hpp>
#include <mavros_msgs/msg/detail/command_code__struct.hpp>
#include <mavros_msgs/msg/detail/position_target__struct.hpp>
#include <mavros_msgs/msg/detail/state__struct.hpp>
#include <mavros_msgs/msg/position_target.hpp>
#include <mavros_msgs/srv/command_long.hpp>
#include <mavros_msgs/srv/detail/command_bool__struct.hpp>
#include <mavros_msgs/srv/detail/command_long__struct.hpp>
#include <mavros_msgs/srv/detail/set_mode__struct.hpp>
#include <rclcpp/client.hpp>
#include <rclcpp/clock.hpp>
#include <rclcpp/duration.hpp>
#include <rclcpp/executors.hpp>
#include <rclcpp/future_return_code.hpp>
#include <rclcpp/logging.hpp>
#include <rclcpp/publisher.hpp>
#include <rclcpp/qos.hpp>
#include <rclcpp/rate.hpp>
#include <rclcpp/service.hpp>
#include <rclcpp/subscription.hpp>
#include <rclcpp/utilities.hpp>
#include <rmw/types.h>
#include <sensor_msgs/msg/detail/nav_sat_fix__struct.hpp>
#include <std_msgs/msg/bool.hpp>
#include <std_msgs/msg/detail/float64__struct.hpp>
#include <std_msgs/msg/float64.hpp>

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

  uav_funcs["takeoff"] = [this](const YAML::Node &cmd) {
    double alt = cmd["altitude"] ? cmd["altitude"].as<double>() : 1.0;
    this->takeoff(alt);
  };

  uav_funcs["switch_mode"] = [this](const YAML::Node &cmd) {
    std::string mode = cmd["mode"] ? cmd["mode"].as<std::string>() : "guided";
    this->switch_mode(mode);
  };

  uav_funcs["wait"] = [this](const YAML::Node &cmd) {
    double s = cmd["s"] ? cmd["s"].as<double>() : 0.0;
    this->wait(s);
  };

  uav_funcs["move_servo"] = [this](const YAML::Node &cmd) {
    int channel = cmd["channel"] ? cmd["channel"].as<int>() : 16;
    int pwm = cmd["pwm"] ? cmd["pwm"].as<int>() : 1000;
    this->move_servo(channel, pwm);
  };

  uav_funcs["heading"] = [this](const YAML::Node &cmd) {
    int type = cmd["type"] ? cmd["type"].as<int>() : 0;
    double deg = cmd["deg"] ? cmd["deg"].as<int>() : 0.0;
    this->heading(type, deg);
  };

  uav_funcs["set_position"] = [this](const YAML::Node &cmd) {
    double x = cmd["x"] ? cmd["x"].as<double>() : 0.0;
    double y = cmd["y"] ? cmd["y"].as<double>() : 0.0;
    double z = cmd["z"] ? cmd["z"].as<double>() : 0.0;
    this->set_position(x, y, z);
  };

  uav_funcs["arm_throttle"] = std::bind(&UAV_Mission::arm_throttle, this);
  uav_funcs["init"] = std::bind(&UAV_Mission::init, this);
  uav_funcs["land"] = std::bind(&UAV_Mission::land, this);
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

void UAV_Mission::wait(double second) {

  RCLCPP_WARN_STREAM(nh->get_logger(),
                     "---Waiting for " << second << " seconds.");

  rclcpp::Time initialTime;
  rclcpp::Time currTime;

  // Ensure node time is initialized
  do {
    initialTime = nh->now();
  } while (initialTime.seconds() < 1.0);

  rclcpp::Rate rate(20); // 20 Hz

  do {
    currTime = nh->now();
    rate.sleep();
  } while ((currTime - initialTime).seconds() < second);
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

double UAV_Mission::degree_to_rad(double degrees) {
  return degrees * M_PI / 180.0;
}

void UAV_Mission::heading(int type, double target) {
  rclcpp::Publisher<mavros_msgs::msg::PositionTarget>::SharedPtr pub;
  mavros_msgs::msg::PositionTarget msg;
  std_msgs::msg::Float64 heading_msg;
  pub = nh->create_publisher<mavros_msgs::msg::PositionTarget>(
      "/mavros/setpoint_raw/local", 10);
  msg.type_mask = 2503;
  msg.coordinate_frame = 1;
  msg.velocity.x = 0.0;
  msg.velocity.y = 0.0;
  msg.velocity.z = 0.0;

  getTopicVal(heading_msg, "/mavros/global_position/compass_hdg",
              std::chrono::seconds(1));
  double init_heading = heading_msg.data;
  msg.yaw = degree_to_rad(
      (double)(target * -1 + 90.0 + type * (init_heading * -1 + 90.0)));

  if (!type) {
    rclcpp::Rate rate(10); // 50 Hz loop
    while (abs(target - heading_msg.data) > 10) {
      getTopicVal(heading_msg, "/mavros/global_position/compass_hdg",
                  std::chrono::seconds(1));

      pub->publish(msg);
      RCLCPP_INFO_STREAM(nh->get_logger(), "Heading set to " << target);
      rclcpp::spin_some(nh); // process timer callbacks
      rate.sleep();
    }
  } else {
    rclcpp::Rate rate(10); // 50 Hz loop

    double heading2 = std::fmod(target + init_heading, 360.0);

    if (heading2 < 0.0)
      heading2 += 360.0;
    while (abs(heading2 - heading_msg.data) > 10) {
      getTopicVal(heading_msg, "/mavros/global_position/compass_hdg",
                  std::chrono::seconds(1));
      heading2 = std::fmod(target + init_heading, 360.0);

      if (heading2 < 0.0)
        heading2 += 360.0;

      pub->publish(msg);
      RCLCPP_INFO_STREAM(nh->get_logger(), "Heading 2 set to " << heading2);
      rclcpp::spin_some(nh); // process timer callbacks
      rate.sleep();
    }
  }

  rclcpp::sleep_for(std::chrono::milliseconds(400));
}

void UAV_Mission::set_position(double x, double y, double z) {
  rclcpp::Publisher<mavros_msgs::msg::PositionTarget>::SharedPtr pub;
  pub = nh->create_publisher<mavros_msgs::msg::PositionTarget>(
      "/mavros/setpoint_raw/local", 10);

  geometry_msgs::msg::PoseStamped pose_msg;

  geometry_msgs::msg::PoseStamped pose_start;

  mavros_msgs::msg::PositionTarget post_target;

  getTopicVal(pose_start, "/mavros/local_position/pose",
              std::chrono::seconds(1));

  post_target.coordinate_frame = 1;
  post_target.header.stamp.sec = nh->now().seconds();
  post_target.type_mask = 1528;
  post_target.position.x = x + pose_start.pose.position.x;
  post_target.position.y = y + pose_start.pose.position.y;
  post_target.position.z = z + pose_start.pose.position.z;

  post_target.velocity.x = 0;
  post_target.velocity.y = 0;
  post_target.velocity.z = 0;

  post_target.acceleration_or_force.x = 0.0;
  post_target.acceleration_or_force.y = 0.0;
  post_target.acceleration_or_force.z = 0.0;

  post_target.yaw = 0.0;
  post_target.yaw_rate = 0.0;

  rclcpp::Rate rate(10); // 50 Hz loop
  getTopicVal(pose_msg, "/mavros/local_position/pose", std::chrono::seconds(1));
  while (abs(x + pose_start.pose.position.x - pose_msg.pose.position.x) > 0.1 ||
         abs(y + pose_start.pose.position.y - pose_msg.pose.position.y) > 0.1 ||
         abs(z + pose_start.pose.position.z - pose_msg.pose.position.z) > 0.1) {
    getTopicVal(pose_msg, "/mavros/local_position/pose",
                std::chrono::seconds(1));

    pub->publish(post_target);
    rclcpp::spin_some(nh); // process timer callbacks
    rate.sleep();
  }

  rclcpp::sleep_for(std::chrono::milliseconds(400));
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

void UAV_Mission::move_servo(int channel, int pwm) {
  rclcpp::Client<mavros_msgs::srv::CommandLong>::SharedPtr client =
      nh->create_client<mavros_msgs::srv::CommandLong>("/mavros/cmd/command");
  mavros_msgs::srv::CommandLong_Request::SharedPtr req =
      std::make_shared<mavros_msgs::srv::CommandLong::Request>();
  req->command = 183;
  req->param1 = channel;
  req->param2 = pwm;

  double start_time = nh->now().seconds();

  auto ftr = client->async_send_request(req);
  auto status =
      rclcpp::spin_until_future_complete(nh, ftr, std::chrono::seconds(1));

  if (status == rclcpp::FutureReturnCode::SUCCESS) {
    RCLCPP_INFO_STREAM(nh->get_logger(),
                       "--- Servo " << channel << " Moving to " << pwm);
  } else {
    RCLCPP_ERROR_STREAM(nh->get_logger(),
                        "--- Servo " << channel << " Failed " << pwm);
  }
}

void UAV_Mission::takeoff(double target_height) {
  rclcpp::Client<mavros_msgs::srv::CommandTOL>::SharedPtr takeoff_client_;
  takeoff_client_ =
      nh->create_client<mavros_msgs::srv::CommandTOL>("/mavros/cmd/takeoff");
  while (!takeoff_client_->wait_for_service(std::chrono::seconds(1))) {
    RCLCPP_WARN(nh->get_logger(), "Waiting for takeoff service...");
  }

  auto request = std::make_shared<mavros_msgs::srv::CommandTOL::Request>();
  request->altitude = target_height;
  request->min_pitch = 0;
  request->yaw = 0;
  request->latitude = 0;
  request->longitude = 0;

  double current_alt = 0;
  bool is_takeoff = false; // True only if service called
  int count =
      0; // Count how many retry if > 5 then cancel mission (something wrong)

  bool tmp_check = getAlt(current_alt, "local_pose");

  while (target_height - current_alt > 0.3 && count < 5) {
    auto future = takeoff_client_->async_send_request(request);
    auto status =
        rclcpp::spin_until_future_complete(nh, future, std::chrono::seconds(3));
    if (status == rclcpp::FutureReturnCode::SUCCESS) {
      auto res = future.get();

      getAlt(current_alt, "local_pose");

      // Once only, atleast we know it's called (otherwise it will spamming)
      if (!is_takeoff) {
        RCLCPP_INFO(nh->get_logger(),
                    "Takeoff service response: success=%d, result=%d",
                    res->success, res->result);
      }
      is_takeoff = true;
    } else if (status == rclcpp::FutureReturnCode::TIMEOUT) {
      RCLCPP_ERROR(nh->get_logger(), "Takeoff service timed out, retrying ...");
      count++;
    } else {
      RCLCPP_ERROR(nh->get_logger(), "Takeoff service failed, retrying...");
      count++;
    }
  }

  if (target_height - current_alt > 0.3) {
    RCLCPP_FATAL_STREAM(nh->get_logger(), "Terminate in function " << __func__);
  } else {
    RCLCPP_INFO_STREAM(nh->get_logger(),
                       "Target Altitude reached: " << current_alt);
  }
}

bool UAV_Mission::getAlt(double &alt, std::string source) {
  if (source == "local_pose") {
    std::string topic_name = "/mavros/local_position/pose";
    geometry_msgs::msg::PoseStamped local_pose;
    bool got_topic =
        getTopicVal(local_pose, topic_name, std::chrono::seconds(5));

    if (!got_topic) {
      RCLCPP_ERROR(nh->get_logger(), "Reading Altitude error...");
    }

    alt = local_pose.pose.position.z;
  }

  else if (source == "rangefinder") {
  }

  return false;
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
