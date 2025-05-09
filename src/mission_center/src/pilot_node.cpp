#include "mission_center/uav_mission.hpp"
#include <std_srvs/srv/detail/empty__struct.hpp>
#include <std_srvs/srv/set_bool.hpp>

class PilotNode : public rclcpp::Node {
public:
  PilotNode() : Node("pilot_node") {
    // Create the service to kill the pilot node
    service_ = this->create_service<std_srvs::srv::Empty>(
        "/pilot_node/kill_pilot",
        std::bind(&PilotNode::killPilotCb, this, std::placeholders::_1,
                  std::placeholders::_2));
    RCLCPP_INFO(this->get_logger(), "Pilot node initialized.");
  }

private:
  rclcpp::Service<std_srvs::srv::Empty>::SharedPtr service_;

  void restartMissionCb(const std::shared_ptr<std_srvs::srv::Empty::Request>,
                        std::shared_ptr<std_srvs::srv::Empty::Response>) {}

  bool startMission = false;

  bool killPilotCb(const std::shared_ptr<std_srvs::srv::Empty::Request> request,
                   std::shared_ptr<std_srvs::srv::Empty::Response> response) {
    RCLCPP_WARN(this->get_logger(), "--- Pilot Node Die ---");
    rclcpp::shutdown();
    return true;
  }

  bool
  toggleStartCb(const std::shared_ptr<std_srvs::srv::SetBool_Request> request,
                std::shared_ptr<std_srvs::srv::SetBool_Response> response) {
    response->success = false;
    this->startMission = request->data;
    response->success = true;
    RCLCPP_INFO(this->get_logger(), "--- Toggle On. Starting Mission ---");
    return true;
  }
};

int main(int argc, char *argv[]) {
  rclcpp::init(argc, argv);
  rclcpp::Node::SharedPtr nh = rclcpp::Node::make_shared("pilot_node");
  UAV_Mission u_mission(nh);
  // Create the node object
  auto node = std::make_shared<PilotNode>();

  node->declare_parameter(
      "config_filepath",
      "/home/gadzz/mission.yaml"); // Always use absolute path (don't use
                                   // ~/...)
  std::string file_path = node->get_parameter("config_filepath").as_string();

  u_mission.load_mission(file_path);
  rclcpp::executors::MultiThreadedExecutor executor;

  executor.add_node(node);
  // Spin the executor in a separate thread
  std::thread executor_thread([&executor]() { executor.spin(); });

  // Wait for the executor thread to finish
  executor_thread.join();

  // Shutdown
  rclcpp::shutdown();
  return 0;
}
