"""Launch the real demo talker with explicit node and topic substitutions."""
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument('node_name'),
        DeclareLaunchArgument('node_namespace'),
        DeclareLaunchArgument('output_topic'),
        Node(package='demo_nodes_cpp',executable='talker',
             name=LaunchConfiguration('node_name'),namespace=LaunchConfiguration('node_namespace'),
             remappings=[('chatter',LaunchConfiguration('output_topic'))],output='screen',emulate_tty=False),
    ])
