#!/usr/bin/env python3

import rclpy
from rclpy.node import Node
from std_msgs.msg import String


class RclpyCliNode(Node):
    def __init__(self) -> None:
        super().__init__("rclpy_cli_node")
        self.declare_parameter("demo_text", "hello from rk3588s")
        self.publisher = self.create_publisher(String, "/rclpy_cli_topic", 10)
        self.subscription = self.create_subscription(
            String,
            "/rclpy_cli_in",
            self._on_message,
            10,
        )
        self.timer = self.create_timer(0.5, self._on_timer)

    def _on_timer(self) -> None:
        msg = String()
        msg.data = self.get_parameter("demo_text").value
        self.publisher.publish(msg)

    def _on_message(self, msg: String) -> None:
        self.get_logger().info(f"rclpy_cli_in_received={msg.data}")


def main() -> None:
    rclpy.init()
    node = RclpyCliNode()
    try:
        node.get_logger().info("rclpy_cli_node_started")
        rclpy.spin(node)
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
