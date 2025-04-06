cmake_minimum_required(VERSION 3.8)
project(mission_center)

# Enable compile_commands.json generation (useful for clangd)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Additional compiler flags for Clang and GCC
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Find dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(std_srvs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(keyboard_msgs REQUIRED)
find_package(mavros_msgs REQUIRED)
find_package(keyboard REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)

# Include directories
include_directories(include)

# Add ImGui (Manual Import)
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
include_directories(${IMGUI_DIR})

# Add ImGui source files (core + backends)
set(IMGUI_SOURCES
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
  ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

# Create the library
add_library(uav_mission SHARED src/uav_mission.cpp)
include_directories(${keyboard_msgs_INCLUDE_DIRS})

# Specify dependencies for the library
ament_target_dependencies(uav_mission
  rclcpp
  std_msgs
  std_srvs
  geometry_msgs
  keyboard_msgs
  keyboard
  mavros_msgs
)

# Add executables
add_executable(pilot_node src/pilot_node.cpp)
add_executable(safety_node src/safety_node.cpp)
add_executable(ui_node src/ui_node.cpp ${IMGUI_SOURCES})

# Link dependencies with your executable
ament_target_dependencies(pilot_node
  rclcpp
  std_msgs
  std_srvs
  geometry_msgs
  keyboard_msgs
  keyboard
)

ament_target_dependencies(safety_node
  rclcpp
  std_msgs
  std_srvs
  geometry_msgs
)

ament_target_dependencies(ui_node
  rclcpp
  std_msgs
  std_srvs
  geometry_msgs
)

# Link the custom library AFTER setting dependencies
target_link_libraries(pilot_node uav_mission yaml-cpp)
target_link_libraries(ui_node uav_mission glfw OpenGL::GL)

# Install the library
install(TARGETS uav_mission
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

# Install the executables
install(TARGETS pilot_node safety_node ui_node
  DESTINATION lib/${PROJECT_NAME}
)

# Export include directories
ament_export_include_directories(include)
ament_export_libraries(uav_mission)

# Test setup if BUILD_TESTING is enabled
if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  set(ament_cmake_copyright_FOUND TRUE)
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

# Create the package
ament_package()
