cmake_minimum_required(VERSION 3.16)
project(mdds_cli_fixture LANGUAGES NONE)

# An installable integration-test package, kept above the RMW dependency layer.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/mdds_cli_fixture" "")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/mdds_cli_fixture"
  DESTINATION share/ament_index/resource_index/packages)
install(FILES package.xml peer_talker_test.py DESTINATION share/mdds_cli_fixture)
