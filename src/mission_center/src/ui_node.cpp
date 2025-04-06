#include <geometry_msgs/msg/twist.hpp>
#include <rclcpp/rclcpp.hpp>

#define IMGUI_IMPL_OPENGL_LOADER_GLAD
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <imgui.h>

#include <GLFW/glfw3.h>

class Ros2ImGuiUI : public rclcpp::Node {
public:
  Ros2ImGuiUI() : Node("ros2_imgui_ui") {
    pub_ = this->create_publisher<geometry_msgs::msg::Twist>("/cmd_vel", 10);
    initGLFW();
    runUI();
  }

private:
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr pub_;
  float linear_x_ = 0.0f, angular_z_ = 0.0f;

  void initGLFW() {
    if (!glfwInit())
      throw std::runtime_error("Failed to initialize GLFW");

    window_ = glfwCreateWindow(600, 400, "ROS2 ImGui UI", NULL, NULL);
    if (!window_)
      throw std::runtime_error("Failed to create GLFW window");

    glfwMakeContextCurrent(window_);
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330");
  }

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

    ImGui::Begin("Mission Control", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoCollapse);
    ImGui::SliderFloat("Linear Speed", &linear_x_, -2.0f, 2.0f);
    ImGui::SliderFloat("Angular Speed", &angular_z_, -1.0f, 1.0f);

    if (ImGui::Button("Send Command")) {
      geometry_msgs::msg::Twist msg;
      msg.linear.x = linear_x_;
      msg.angular.z = angular_z_;
      pub_->publish(msg);
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

  GLFWwindow *window_;
};

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<Ros2ImGuiUI>());
  rclcpp::shutdown();
  return 0;
}
