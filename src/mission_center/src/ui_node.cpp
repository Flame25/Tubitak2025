#include <chrono>
#include <geometry_msgs/msg/twist.hpp>
#include <rclcpp/executors.hpp>
#include <rclcpp/logging.hpp>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/bool.hpp>
#include <std_srvs/srv/detail/empty__struct.hpp>
#include <std_srvs/srv/empty.hpp>

#define IMGUI_IMPL_OPENGL_LOADER_GLAD
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <imgui.h>

#include <GLFW/glfw3.h>

class Ros2ImGuiUI : public rclcpp::Node,
                    public std::enable_shared_from_this<Ros2ImGuiUI> {
public:
  Ros2ImGuiUI() : Node("ros2_imgui_ui") {
    startPub = this->create_publisher<std_msgs::msg::Bool>(
        "mission_center/start_mission", 10);
    client_restartMission = this->create_client<std_srvs::srv::Empty>(
        "/pilot_node/restart_mission");
    client_killPilot =
        this->create_client<std_srvs::srv::Empty>("/pilot_node/kill_pilot");
    initGLFW();
    runUI();
  }

private:
  rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr startPub;
  rclcpp::Client<std_srvs::srv::Empty>::SharedPtr client_restartMission;
  rclcpp::Client<std_srvs::srv::Empty>::SharedPtr client_killPilot;
  std::mutex service_mutex;

  GLFWwindow *window_;

  // Initialize GLFW and ImGui
  void initGLFW() {
    if (!glfwInit())
      throw std::runtime_error("Failed to initialize GLFW");

    window_ = glfwCreateWindow(130, 100, "ROS2 ImGui UI", NULL, NULL);
    if (!window_)
      throw std::runtime_error("Failed to create GLFW window");

    glfwMakeContextCurrent(window_);
    glfwSetWindowAttrib(window_, GLFW_RESIZABLE, GLFW_TRUE);

    // Initialize ImGui context
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330");
  }

  // Run the UI loop
  void runUI() {
    while (!glfwWindowShouldClose(window_) && rclcpp::ok()) {
      glfwPollEvents();
      renderUI();
      rclcpp::spin_some(this->get_node_base_interface());
    }
    cleanup();
  }

  void renderUI() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowSize(ImVec2(130, 100), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::Begin("Mission Control", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoCollapse);

    // Get the width of the window and the button
    float windowWidth = ImGui::GetWindowWidth();
    float buttonWidth =
        ImGui::GetTextLineHeightWithSpacing() * 6; // Approximate button width

    // Set the cursor position to center the button horizontally
    ImGui::SetCursorPosX((windowWidth - buttonWidth) * 0.5f);

    if (ImGui::Button("Start Mission")) {
      std_msgs::msg::Bool msg;
      msg.data = true;
      startPub->publish(msg);
    }

    if (client_restartMission->wait_for_service(std::chrono::seconds(1))) {
      if (ImGui::Button("Restart Mission")) {
        try {
          auto request = std::make_shared<std_srvs::srv::Empty::Request>();
          auto future = client_restartMission->async_send_request(request);
          if (rclcpp::spin_until_future_complete(
                  this->get_node_base_interface(), future) ==
              rclcpp::FutureReturnCode::SUCCESS) {
            RCLCPP_INFO(this->get_logger(), "Service successfully called.");
          } else {
            RCLCPP_ERROR(this->get_logger(), "Failed to call service.");
          }
        } catch (const std::exception &e) {
          RCLCPP_ERROR(this->get_logger(), "Exception: %s", e.what());
        }
      }
    }

    if (client_killPilot->wait_for_service(std::chrono::seconds(1))) {
      if (ImGui::Button("Kill Pilot")) {
        try {
          auto request = std::make_shared<std_srvs::srv::Empty::Request>();
          auto future = client_killPilot->async_send_request(request);
          if (rclcpp::spin_until_future_complete(
                  this->get_node_base_interface(), future) ==
              rclcpp::FutureReturnCode::SUCCESS) {
            RCLCPP_INFO(this->get_logger(), "Service successfully called.");
          } else {
            RCLCPP_ERROR(this->get_logger(), "Failed to call service.");
          }
        } catch (const std::exception &e) {
          RCLCPP_ERROR(this->get_logger(), "Exception: %s", e.what());
        }
      }
    }

    ImGui::End();
    ImGui::Render();

    int display_w, display_h;
    glfwGetFramebufferSize(window_, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);

    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window_);
  }

  void cleanup() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window_);
    glfwTerminate();
  }
};

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<Ros2ImGuiUI>());
  rclcpp::shutdown();
  return 0;
}
