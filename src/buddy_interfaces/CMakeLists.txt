cmake_minimum_required(VERSION 3.8)
project(buddy_interfaces)

find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(action_msgs REQUIRED)

find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)

install(DIRECTORY include/ DESTINATION include)

ament_export_include_directories(include)

rosidl_generate_interfaces(
  ${PROJECT_NAME}
  "msg/InferenceChunk.msg"
  "msg/Sentence.msg"
  "msg/EmotionResult.msg"
  "msg/InferenceRequest.msg"
  "msg/TtsControl.msg"
  "srv/CaptureImage.srv"
  "action/Inference.action"
  DEPENDENCIES
  builtin_interfaces
  sensor_msgs
  std_msgs
  action_msgs
)

ament_package()
