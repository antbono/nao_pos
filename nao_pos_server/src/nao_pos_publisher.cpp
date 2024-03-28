#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

using namespace std::chrono_literals;

/* This example creates a subclass of Node and uses std::bind() to register a
* member function as a callback from the timer. */

class NaoPosPublisher : public rclcpp::Node
{
  public:
    NaoPosPublisher()
    : Node("nao_pos_publisher")
    {
      publisher_ = this->create_publisher<std_msgs::msg::String>("action_req", 10);
      timer_ = this->create_wall_timer(
      10000ms, std::bind(&NaoPosPublisher::timer_callback, this));
    }

  private:
    void timer_callback()
    {
      auto message = std_msgs::msg::String();
      message.data = "only_legs";
      RCLCPP_INFO(this->get_logger(), "Publishing: '%s'", message.data.c_str());
      publisher_->publish(message);
    }
    rclcpp::TimerBase::SharedPtr timer_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<NaoPosPublisher>());
  rclcpp::shutdown();
  return 0;
}