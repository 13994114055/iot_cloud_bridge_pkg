cmake_minimum_required(VERSION 3.8)
project(iot_cloud_bridge_pkg)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# find dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(emotion_msgs REQUIRED)
find_package(nlohmann_json REQUIRED)

# 交叉编译时从 X5 sysroot 查找 OpenSSL
if(DEFINED X5_SYSROOT AND CMAKE_CROSSCOMPILING)
  list(APPEND CMAKE_FIND_ROOT_PATH "${X5_SYSROOT}")
  find_package(OpenSSL REQUIRED)
  # 覆盖为 sysroot 中的 aarch64 库
  set_target_properties(OpenSSL::SSL PROPERTIES
    IMPORTED_LOCATION "${X5_SYSROOT}/lib/aarch64-linux-gnu/libssl.a"
    INTERFACE_INCLUDE_DIRECTORIES "${X5_SYSROOT}/include"
  )
  set_target_properties(OpenSSL::Crypto PROPERTIES
    IMPORTED_LOCATION "${X5_SYSROOT}/lib/aarch64-linux-gnu/libcrypto.a"
    INTERFACE_INCLUDE_DIRECTORIES "${X5_SYSROOT}/include"
  )
  include_directories("${X5_SYSROOT}/include/aarch64-linux-gnu")
else()
  find_package(OpenSSL REQUIRED)
endif()

include_directories(include)

# 串口工具库
add_library(serial_utils src/serial_utils.cpp)
target_include_directories(serial_utils PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(serial_utils)

# 主节点
add_executable(cloud_upload_node src/cloud_upload_node.cpp)
target_include_directories(cloud_upload_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
ament_target_dependencies(cloud_upload_node
  rclcpp
  std_msgs
  sensor_msgs
  emotion_msgs)
target_link_libraries(cloud_upload_node
  serial_utils
  nlohmann_json::nlohmann_json
  OpenSSL::SSL
  OpenSSL::Crypto
  pthread)

install(TARGETS serial_utils cloud_upload_node
  DESTINATION lib/${PROJECT_NAME})

install(DIRECTORY config launch
  DESTINATION share/${PROJECT_NAME})

install(FILES
  package.xml
  DESTINATION share/${PROJECT_NAME})

ament_package()
