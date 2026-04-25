import os
from launch import LaunchDescription
from launch_ros.actions import LifecycleNode
from ament_index_python.packages import get_package_share_directory


def generate_launch_description():
    params_file = os.path.join(
        get_package_share_directory('buddy_bringup'), 'params', 'buddy_params.yaml')

    audio_node = LifecycleNode(
        package='buddy_audio',
        executable='audio_node',
        name='audio',
        namespace='',
        parameters=[params_file],
        output='screen',
    )

    vision_node = LifecycleNode(
        package='buddy_vision',
        executable='vision_node',
        name='vision',
        namespace='',
        parameters=[params_file],
        output='screen',
    )

    cloud_node = LifecycleNode(
        package='buddy_cloud',
        executable='cloud_node',
        name='cloud',
        namespace='',
        parameters=[params_file],
        output='screen',
    )

    state_machine_node = LifecycleNode(
        package='buddy_state_machine',
        executable='state_machine_node',
        name='state_machine',
        namespace='',
        parameters=[params_file],
        output='screen',
    )

    dialog_node = LifecycleNode(
        package='buddy_dialog',
        executable='dialog_node',
        name='dialog',
        namespace='',
        parameters=[params_file],
        output='screen',
    )

    sentence_node = LifecycleNode(
        package='buddy_sentence',
        executable='sentence_node',
        name='sentence',
        namespace='',
        parameters=[params_file],
        output='screen',
    )

    return LaunchDescription([
        audio_node,
        vision_node,
        cloud_node,
        state_machine_node,
        dialog_node,
        sentence_node,
    ])
