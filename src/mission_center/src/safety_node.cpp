#include <mavros_msgs/msg/rc_in.hpp>
#include <memory>
#include <rclcpp/executors.hpp>
#include <rclcpp/logging.hpp>
#include <rclcpp/publisher.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp/utilities.hpp>
#include <std_msgs/msg/bool.hpp>
#include <std_msgs/msg/detail/bool__struct.hpp>
#include <std_srvs/srv/detail/empty__struct.hpp>
#include <std_srvs/srv/empty.hpp>

#define RC_CHECK 0

// Change to the radio channel
#define RC_LAND 6
#define RC_STABILIZE 6
#define RC_LOITER 6

const std::string RC_LAND_PWM = "HIGH";
const std::string RC_STABILIZE_PWM = "HIGH";
const std::string RC_LOITER_PWM = "HIGH";

rclcpp::Node::SharedPtr nh; // Declare node as a shared pointer
rclcpp::Client<std_srvs::srv::Empty>::SharedPtr client_killPilot;

bool checkPilot() {
  // Wait until the service is available
  while (!client_killPilot->wait_for_service(std::chrono::seconds(1))) {
    if (!rclcpp::ok()) {
      RCLCPP_ERROR(nh->get_logger(),
                   "Interrupted while waiting for the service. Exiting.");
      return false;
    }
    RCLCPP_INFO(nh->get_logger(), "Waiting for service...");
  }

  RCLCPP_INFO(nh->get_logger(), "--- Service is Available ---");

  return true;
}

void callKillPilotService() {
  // Create a request (empty in this case)
  auto request = std::make_shared<std_srvs::srv::Empty::Request>();

  // Call the service
  auto future = client_killPilot->async_send_request(request);

  // Wait for the response
  if (rclcpp::spin_until_future_complete(nh, future) ==
      rclcpp::FutureReturnCode::SUCCESS) {
    RCLCPP_INFO(nh->get_logger(), "Service successfully called.");
  } else {
    RCLCPP_ERROR(nh->get_logger(), "Failed to call service.");
  }
}

std::shared_ptr<mavros_msgs::msg::RCIn>
wait_for_rc_in(rclcpp::Node::SharedPtr node, const std::string &topic,
               std::chrono::seconds timeout) {
  using mavros_msgs::msg::RCIn;
  std::shared_ptr<RCIn> msg = nullptr;
  std::promise<std::shared_ptr<RCIn>> prom;
  auto future = prom.get_future();

  auto sub = node->create_subscription<RCIn>(
      topic, 10, [&prom](RCIn::SharedPtr m) { prom.set_value(m); });

  auto status = future.wait_for(timeout);
  if (status == std::future_status::ready) {
    msg = future.get();
  }
  return msg;
}

bool rcPWM(int val, const std::string &want) {
  if (want == "HIGH") {
    if (val > 2000)
      return true;
    else
      return false;
  } else if (want == "LOW") {
    if (val < 1000)
      return true;
    else
      return false;
  } else {
    RCLCPP_FATAL(nh->get_logger(), "CHECK rcPWM func!");
    throw std::invalid_argument("CHECK rcPWM func!");
  }
}

int rcTrigger() {
#if RC_CHECK == 1
  auto rcInPtr = wait_for_rc_in(nh, "/mavros/rc/in", std::chrono::seconds(5));
  if (rcInPtr == NULL) {
    RCLCPP_FATAL_STREAM(nh->get_logger(), "Is RC connected?");
    return 0;
  }

  if (rcPWM(rcInPtr->channels.at(RC_LAND - 1), RC_LAND_PWM)) {
    RCLCPP_ERROR(nh->get_logger(), "RC LAND TRIGGERED");
    return 1;
  }
  if (rcPWM(rcInPtr->channels.at(RC_STABILIZE - 1), RC_STABILIZE_PWM)) {
    RCLCPP_ERROR(nh->get_logger(), "RC STABILIZE TRIGGERED");
    return 2;
  }
  // if (rcPWM(rcInPtr->channels.at(RC_N_EMERGENCY_STOP_MOTOR - 1),
  //           RC_STAB_EMERGENCY_STOP_MOTOR)) {
  //   ROS_ERROR("RC EMERGENCY STOP MOTOR TRIGGERED");
  //   return 3;
  // }

#endif

  return 0;
}

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);

  nh = rclcpp::Node::make_shared("safety_node"); // Create the node after init
  rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr pub =
      nh->create_publisher<std_msgs::msg::Bool>("/safety_node/active", 10);

  while (rclcpp::ok()) {
    // Create the client for the "killPilot" service
    client_killPilot =
        nh->create_client<std_srvs::srv::Empty>("/pilot_node/kill_pilot");

    // Create the executor to handle spinning (do this only once)
    rclcpp::executors::SingleThreadedExecutor executor;

    // Now we can call the checkPilot function to ensure the service is
    // available
    if (checkPilot()) {
      RCLCPP_INFO(nh->get_logger(),
                  "Service is available. Calling the service...");
      // callKillPilotService(); // Call the service if available
    } else {
      RCLCPP_ERROR(nh->get_logger(), "Service is not available.");
    }

    int triggeredRC = rcTrigger();
    if (triggeredRC) {
      RCLCPP_FATAL_STREAM(nh->get_logger(), "Initiating failsafe");
      if (checkPilot()) {
        RCLCPP_ERROR(nh->get_logger(), "Pilot will be killed");
        auto request = std::make_shared<std_srvs::srv::Empty::Request>();
        client_killPilot->async_send_request(request);
        // ros::Duration(1.0).sleep();    // for safety (?)
      }
    }

    rclcpp::spin(nh);

    rclcpp::shutdown(); // Shutdown after spinning

    return 0;
  }
}
