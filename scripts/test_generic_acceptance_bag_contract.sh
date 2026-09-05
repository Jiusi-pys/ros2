#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
ROS2_OWNED_REMOTE_DIR=/not-executed
topic_base=/contract
selection="$(sed -n '/^bag_name=/,/^ros2_owned_launch /p' scripts/run_ohos_generic_acceptance.sh | sed '$d')"
for bag_language in cpp python; do
  eval "$selection"
  [ "$bag_dir" = "/not-executed/bag_$bag_language" ]
  [ "$bag_topic" = "/contract_bag_$bag_language" ]
  if [ "$bag_language" = cpp ]; then
    [ "$bag_publisher" = '$ROS2_TALKER_RAW' ]
    [ "$bag_subscriber" = '$ROS2_LISTENER_RAW' ]
  else
    [ "$bag_publisher" = 'python3.12 $ROS2_PY_TALKER_RAW' ]
    [ "$bag_subscriber" = 'python3.12 $ROS2_PY_LISTENER_RAW' ]
  fi
done
echo 'GENERIC_BAG_SELECTION_CONTRACT result=PASS languages=cpp,python'
