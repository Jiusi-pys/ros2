#!/usr/bin/env python3

import argparse
import re
from pathlib import Path
from typing import Any, Dict

import yaml


DOMAIN_TOPIC_PATTERN = re.compile(r"^d[0-9]+/")


def domain_id(value: str) -> int:
    parsed = int(value)
    if parsed < 0 or parsed > 232:
        raise argparse.ArgumentTypeError("domain must be in range 0..232")
    return parsed


def render_config(config: Dict[str, Any], domain: int) -> Dict[str, Any]:
    mappings = config.get("mappings")
    if not isinstance(mappings, list):
        raise ValueError("gateway config template must contain a mappings list")

    prefix = f"d{domain}/" if domain != 0 else ""
    for mapping in mappings:
        if not isinstance(mapping, dict):
            raise ValueError("every gateway mapping must be an object")
        topic = mapping.get("mddsTopicName")
        if not isinstance(topic, str) or not topic:
            raise ValueError("every gateway mapping must contain mddsTopicName")
        mapping["mddsTopicName"] = prefix + DOMAIN_TOPIC_PATTERN.sub("", topic)
    return config


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Render an rmw_mdds gateway YAML config for one ROS domain"
    )
    parser.add_argument("--template", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--domain", required=True, type=domain_id)
    args = parser.parse_args()

    with args.template.open("r", encoding="utf-8") as source:
        config = yaml.safe_load(source)
    if not isinstance(config, dict):
        raise ValueError("gateway config template must contain a YAML object")

    rendered = render_config(config, args.domain)
    with args.output.open("w", encoding="utf-8") as output:
        yaml.safe_dump(rendered, output, sort_keys=False)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
