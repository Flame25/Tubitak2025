// generated from rosidl_generator_c/resource/idl__struct.h.em
// with input from px4_msgs:msg/MecanumVelocitySetpoint.idl
// generated code does not contain a copyright notice

#ifndef PX4_MSGS__MSG__DETAIL__MECANUM_VELOCITY_SETPOINT__STRUCT_H_
#define PX4_MSGS__MSG__DETAIL__MECANUM_VELOCITY_SETPOINT__STRUCT_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


// Constants defined in the message

/// Struct defined in msg/MecanumVelocitySetpoint in the package px4_msgs.
typedef struct px4_msgs__msg__MecanumVelocitySetpoint
{
  /// time since system start (microseconds)
  uint64_t timestamp;
  /// [-inf, inf] Speed setpoint
  float speed;
  /// [-pi, pi] from North.
  float bearing;
  /// [-pi, pi] (Optional, defaults to current vehicle yaw) Vehicle yaw setpoint in NED frame
  float yaw;
} px4_msgs__msg__MecanumVelocitySetpoint;

// Struct for a sequence of px4_msgs__msg__MecanumVelocitySetpoint.
typedef struct px4_msgs__msg__MecanumVelocitySetpoint__Sequence
{
  px4_msgs__msg__MecanumVelocitySetpoint * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} px4_msgs__msg__MecanumVelocitySetpoint__Sequence;

#ifdef __cplusplus
}
#endif

#endif  // PX4_MSGS__MSG__DETAIL__MECANUM_VELOCITY_SETPOINT__STRUCT_H_
