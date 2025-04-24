#include <chrono>
#include <cmath>
#include <memory>

#include "mavros_msgs/msg/position_target.hpp"
#include "rclcpp/rclcpp.hpp"

using namespace std::chrono_literals;
using mavros_msgs::msg::PositionTarget;

class TrajectoryStreamer : public rclcpp::Node {
public:
  TrajectoryStreamer() : Node("trajectory_streamer") {
    pub_ = this->create_publisher<PositionTarget>("/mavros/setpoint_raw/local",
                                                  10);
    timer_ = this->create_wall_timer(
        20ms, std::bind(&TrajectoryStreamer::timer_callback, this));
    start_time_ = this->now();
  }

private:
  void timer_callback() {
    rclcpp::Time now = this->now();
    double t = (now - start_time_).seconds();

    double radius = 4.0;
    double speed = 0.5;

    PositionTarget msg;
    msg.header.stamp = now;
    msg.coordinate_frame = PositionTarget::FRAME_LOCAL_NED;

    // Ignore accel and yaw_rate; send position, velocity, yaw

    msg.type_mask = PositionTarget::IGNORE_AFX | PositionTarget::IGNORE_AFY |
                    PositionTarget::IGNORE_AFZ |
                    PositionTarget::IGNORE_YAW_RATE;

    // Position: circular in XY, constant Z
    msg.position.x = radius * std::cos(speed * t);
    msg.position.y = radius * std::sin(speed * t);
    msg.position.z = 2.0;

    // Velocity: derivative of position
    msg.velocity.x = -radius * speed * std::sin(speed * t);
    msg.velocity.y = radius * speed * std::cos(speed * t);
    msg.velocity.z = 0.0;

    // Yaw (optional)
    msg.yaw = std::atan2(msg.velocity.y, msg.velocity.x);

    pub_->publish(msg);
  }

  rclcpp::Publisher<PositionTarget>::SharedPtr pub_;
  rclcpp::TimerBase::SharedPtr timer_;
  rclcpp::Time start_time_;
};

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<TrajectoryStreamer>());
  rclcpp::shutdown();
  return 0;
}
