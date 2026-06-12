#!/usr/bin/env python3

import rclpy
from rclpy.node import Node
from std_srvs.srv import Trigger


class RclpyCliServiceNode(Node):
    def __init__(self) -> None:
        super().__init__("rclpy_cli_service")
        self._request_count = 0
        self._service = self.create_service(
            Trigger,
            "/rclpy_cli_trigger",
            self._handle_trigger,
        )

    def _handle_trigger(self, request: Trigger.Request, response: Trigger.Response) -> Trigger.Response:
        del request
        self._request_count += 1
        response.success = True
        response.message = f"trigger_count={self._request_count}"
        self.get_logger().info(response.message)
        return response


def main() -> None:
    rclpy.init()
    node = RclpyCliServiceNode()
    try:
        node.get_logger().info("rclpy_cli_service_started")
        rclpy.spin(node)
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
