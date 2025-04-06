#include <rclcpp/executors.hpp>
#include <rclcpp/publisher.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp/utilities.hpp>
#include <std_msgs/msg/bool.hpp>
#include <std_msgs/msg/detail/bool__struct.hpp>
#include <std_srvs/srv/empty.hpp>

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
  }

  rclcpp::spin(nh);

  rclcpp::shutdown(); // Shutdown after spinning

  return 0;
}
