from launch import LaunchDescription
from launch_ros.actions import Node
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration

def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument(
            'config_file',
            default_value='',
            description='Path to config yaml file'
        ),
        Node(
            package='iot_cloud_bridge_cpp',
            executable='cloud_upload_node',
            name='cloud_upload_node',
            output='screen',
            parameters=[LaunchConfiguration('config_file')],
            emulate_tty=True
        )
    ])