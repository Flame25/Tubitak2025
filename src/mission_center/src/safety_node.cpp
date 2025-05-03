#include <mavros_msgs/msg/detail/rc_in__struct.hpp>
#include <mavros_msgs/msg/rc_in.hpp>
#include <memory>
#include <rclcpp/executors.hpp>
#include <rclcpp/logging.hpp>
#include <rclcpp/publisher.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp/utilities.hpp>
#include <rclcpp/wait_for_message.hpp>
#include <std_msgs/msg/bool.hpp>
#include <std_msgs/msg/detail/bool__struct.hpp>
#include <std_srvs/srv/detail/empty__struct.hpp>
#include <std_srvs/srv/empty.hpp>
#include <string>

#define RC_CHECK 1

// Change to the radio channel
#define RC_LAND 8
#define RC_STABILIZE 6
#define RC_LOITER 6

const std::string RC_LAND_PWM = "LOW";
const std::string RC_STABILIZE_PWM = "HIGH";
const std::string RC_LOITER_PWM = "HIGH";

rclcpp::Node::SharedPtr nh; // Declare node as a shared pointer
rclcpp::Client<std_srvs::srv::Empty>::SharedPtr client_killPilot;
bool callReady = false;

bool checkPilot() {
  // Wait until the service is available
  if (!client_killPilot->wait_for_service(std::chrono::seconds(1))) {
    if (!rclcpp::ok()) {
      RCLCPP_ERROR(nh->get_logger(),
                   "Interrupted while waiting for the service. Exiting.");
    }
    RCLCPP_INFO(nh->get_logger(), "Waiting for pilot node...");
    return false;
  }
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

bool rcPWM(int val, const std::string &want) {
  if (want == "HIGH") {
    if (val > 1999)
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

template <typename MessageT>
void wait_for_message(
    std::shared_ptr<MessageT> &msg, rclcpp::Node::SharedPtr node,
    const std::string &topic,
    std::chrono::nanoseconds timeout = std::chrono::seconds(5)) {
  auto sub = node->create_subscription<MessageT>(
      topic, rclcpp::QoS(10),
      [&msg](typename MessageT::SharedPtr m) { msg = m; });

  rclcpp::Time start = node->now();
  rclcpp::Rate rate(10);
  while (rclcpp::ok() && !msg) {
    rclcpp::spin_some(node);
    rate.sleep();
    if (node->now() - start > rclcpp::Duration(timeout)) {
      break;
    }
  }
}
int rcTrigger() {

#if RC_CHECK == 1
  std::shared_ptr<mavros_msgs::msg::RCIn> rcInPtr;
  wait_for_message<mavros_msgs::msg::RCIn>(rcInPtr, nh, "/mavros/rc/in",
                                           std::chrono::seconds(5));

  if (!rcInPtr) {
    RCLCPP_FATAL_STREAM(nh->get_logger(), "Is RC connected?");
    return 0;
  }

  // if (rcPWM(rcInPtr->channels.at(RC_LAND - 1), RC_LAND_PWM)) {
  //   RCLCPP_ERROR(nh->get_logger(), "RC LAND TRIGGERED");
  //   return 1;
  // }
  if (rcPWM(rcInPtr->channels.at(RC_STABILIZE - 1), RC_STABILIZE_PWM)) {
    RCLCPP_ERROR(nh->get_logger(), "RC STABILIZE TRIGGERED");
    return 2;
  }
  // Uncomment if needed
  // if (rcPWM(rcInPtr->channels.at(RC_N_EMERGENCY_STOP_MOTOR - 1),
  //           RC_STAB_EMERGENCY_STOP_MOTOR)) {
  //   RCLCPP_ERROR(nh->get_logger(), "RC EMERGENCY STOP MOTOR TRIGGERED");
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

  // Create the client for the "killPilot" service
  client_killPilot =
      nh->create_client<std_srvs::srv::Empty>("/pilot_node/kill_pilot");

  while (rclcpp::ok()) {

    // Create the executor to handle spinning (do this only once)
    rclcpp::executors::SingleThreadedExecutor executor;

    // Now we can call the checkPilot function to ensure the service is
    // available
    if (!checkPilot()) {
      // callKillPilotService(); // Call the service if available
      RCLCPP_ERROR(nh->get_logger(), "Service is not available.");
      continue;
    }

    if (!callReady) {
      RCLCPP_INFO(nh->get_logger(), "--- Safety Node Ready ---");
      callReady = true;
    }

    int triggeredRC = rcTrigger();
    if (triggeredRC) {
      RCLCPP_FATAL_STREAM(nh->get_logger(), "Initiating failsafe");
      if (checkPilot()) {
        RCLCPP_ERROR(nh->get_logger(), "Pilot will be killed");
        auto request = std::make_shared<std_srvs::srv::Empty::Request>();
        callKillPilotService(); // Call the service if available
        rclcpp::sleep_for(std::chrono::seconds(5));
        rclcpp::spin_some(nh); // Update service availability
        callReady = false;
      }
    }
  }

  rclcpp::spin(nh);

  rclcpp::shutdown(); // Shutdown after spinning
}
