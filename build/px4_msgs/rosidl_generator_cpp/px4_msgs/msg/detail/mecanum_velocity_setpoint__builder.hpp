// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from px4_msgs:msg/MecanumVelocitySetpoint.idl
// generated code does not contain a copyright notice

#ifndef PX4_MSGS__MSG__DETAIL__MECANUM_VELOCITY_SETPOINT__BUILDER_HPP_
#define PX4_MSGS__MSG__DETAIL__MECANUM_VELOCITY_SETPOINT__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "px4_msgs/msg/detail/mecanum_velocity_setpoint__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace px4_msgs
{

namespace msg
{

namespace builder
{

class Init_MecanumVelocitySetpoint_yaw
{
public:
  explicit Init_MecanumVelocitySetpoint_yaw(::px4_msgs::msg::MecanumVelocitySetpoint & msg)
  : msg_(msg)
  {}
  ::px4_msgs::msg::MecanumVelocitySetpoint yaw(::px4_msgs::msg::MecanumVelocitySetpoint::_yaw_type arg)
  {
    msg_.yaw = std::move(arg);
    return std::move(msg_);
  }

private:
  ::px4_msgs::msg::MecanumVelocitySetpoint msg_;
};

class Init_MecanumVelocitySetpoint_bearing
{
public:
  explicit Init_MecanumVelocitySetpoint_bearing(::px4_msgs::msg::MecanumVelocitySetpoint & msg)
  : msg_(msg)
  {}
  Init_MecanumVelocitySetpoint_yaw bearing(::px4_msgs::msg::MecanumVelocitySetpoint::_bearing_type arg)
  {
    msg_.bearing = std::move(arg);
    return Init_MecanumVelocitySetpoint_yaw(msg_);
  }

private:
  ::px4_msgs::msg::MecanumVelocitySetpoint msg_;
};

class Init_MecanumVelocitySetpoint_speed
{
public:
  explicit Init_MecanumVelocitySetpoint_speed(::px4_msgs::msg::MecanumVelocitySetpoint & msg)
  : msg_(msg)
  {}
  Init_MecanumVelocitySetpoint_bearing speed(::px4_msgs::msg::MecanumVelocitySetpoint::_speed_type arg)
  {
    msg_.speed = std::move(arg);
    return Init_MecanumVelocitySetpoint_bearing(msg_);
  }

private:
  ::px4_msgs::msg::MecanumVelocitySetpoint msg_;
};

class Init_MecanumVelocitySetpoint_timestamp
{
public:
  Init_MecanumVelocitySetpoint_timestamp()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_MecanumVelocitySetpoint_speed timestamp(::px4_msgs::msg::MecanumVelocitySetpoint::_timestamp_type arg)
  {
    msg_.timestamp = std::move(arg);
    return Init_MecanumVelocitySetpoint_speed(msg_);
  }

private:
  ::px4_msgs::msg::MecanumVelocitySetpoint msg_;
};

}  // namespace builder

}  // namespace msg

template<typename MessageType>
auto build();

template<>
inline
auto build<::px4_msgs::msg::MecanumVelocitySetpoint>()
{
  return px4_msgs::msg::builder::Init_MecanumVelocitySetpoint_timestamp();
}

}  // namespace px4_msgs

#endif  // PX4_MSGS__MSG__DETAIL__MECANUM_VELOCITY_SETPOINT__BUILDER_HPP_
