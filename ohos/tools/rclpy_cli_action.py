#!/usr/bin/env python3

import rclpy
from rclpy.action import ActionServer
from rclpy.node import Node
from example_interfaces.action import Fibonacci


class RclpyCliActionNode(Node):
    def __init__(self) -> None:
        super().__init__("rclpy_cli_action")
        self._action_server = ActionServer(
            self,
            Fibonacci,
            "/rclpy_cli_fibonacci",
            self._execute_goal,
        )

    def _execute_goal(self, goal_handle) -> Fibonacci.Result:
        order = max(0, int(goal_handle.request.order))
        sequence = [0, 1]
        feedback = Fibonacci.Feedback()

        if order == 0:
            sequence = [0]
        elif order == 1:
            sequence = [0, 1]
        else:
            for _ in range(2, order + 1):
                sequence.append(sequence[-1] + sequence[-2])
                feedback.sequence = sequence[:]
                goal_handle.publish_feedback(feedback)

        goal_handle.succeed()
        result = Fibonacci.Result()
        result.sequence = sequence[: order + 1]
        self.get_logger().info(f"fibonacci_result={result.sequence}")
        return result


def main() -> None:
    rclpy.init()
    node = RclpyCliActionNode()
    try:
        node.get_logger().info("rclpy_cli_action_started")
        rclpy.spin(node)
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
