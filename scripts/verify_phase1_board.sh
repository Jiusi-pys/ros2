#!/bin/sh
# On-board verification for Phase 1+2 (sros2, kdl_parser_py, PyKDL, OpenCV demo).
. /data/local/tmp/ros2/env.sh
cd /data/local/tmp/ros2
VLOG=$ROS2_HOME/verify_logs
mkdir -p $VLOG

echo "== 1. sros2 keystore =="
ros2 security create_keystore $VLOG/keystore > $VLOG/sros2_out.txt 2>&1
ls $VLOG/keystore >/dev/null 2>&1 && echo "SROS2_OK" || { echo "SROS2_FAIL"; cat $VLOG/sros2_out.txt; }

echo "== 2. kdl_parser_py + PyKDL =="
python3.12 - <<'EOF' && echo "KDL_OK" || echo "KDL_FAIL"
from kdl_parser_py.urdf import treeFromString
urdf = '''
<robot name="t">
  <link name="base"/>
  <link name="tip"/>
  <joint name="j1" type="revolute">
    <parent link="base"/><child link="tip"/>
    <origin xyz="0 0 1"/><axis xyz="0 0 1"/>
    <limit lower="-1" upper="1" effort="1" velocity="1"/>
  </joint>
</robot>'''
ok, tree = treeFromString(urdf)
print("ok:", ok, "segments:", tree.getNrOfSegments())
assert ok and tree.getNrOfSegments() == 1
EOF

echo "== 3. tf2_bullet on ament index =="
ros2 pkg list 2>/dev/null | grep -x tf2_bullet && echo "TF2BULLET_OK" || echo "TF2BULLET_FAIL"

echo "== 4. image_tools cam2image (burger mode, no camera) =="
timeout 12 $ROS2_HOME/Lib/image_tools/cam2image --ros-args -p burger_mode:=true -p width:=64 -p height:=64 > $VLOG/cam2image.txt 2>&1
grep -q "Publishing image" $VLOG/cam2image.txt && echo "CAM2IMAGE_OK" || { echo "CAM2IMAGE_FAIL"; head -5 $VLOG/cam2image.txt; }

echo "== 5. loopback regression =="
nohup $ROS2_LISTENER > $VLOG/vfy_listener.log 2>&1 &
sleep 3
nohup $ROS2_TALKER > $VLOG/vfy_talker.log 2>&1 &
sleep 10
pkill -f "$ROS2_HOME/Lib/demo_nodes_cpp/" 2>/dev/null
heard=$(grep -c "I heard" $VLOG/vfy_listener.log)
echo "listener heard: $heard"
[ "$heard" -gt 0 ] && echo "LOOPBACK_OK" || echo "LOOPBACK_FAIL"

echo "== done =="
