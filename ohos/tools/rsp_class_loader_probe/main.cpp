#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "class_loader/class_loader.hpp"
#include "class_loader/class_loader_core.hpp"
#include "console_bridge/console.h"
#include "rclcpp_components/node_factory.hpp"

int main(int argc, char * argv[])
{
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " <library-path> <class-name>\n";
    return 2;
  }

  const std::string library_path = argv[1];
  const std::string class_name = argv[2];

  console_bridge::setLogLevel(console_bridge::CONSOLE_BRIDGE_LOG_DEBUG);

  try {
    class_loader::ClassLoader loader(library_path);
    std::vector<std::string> classes =
      loader.getAvailableClasses<rclcpp_components::NodeFactory>();

    std::cout << "library=" << library_path << "\n";
    std::cout << "class=" << class_name << "\n";
    std::cout << "available_classes=" << classes.size() << "\n";
    for (const auto & entry : classes) {
      std::cout << "  " << entry << "\n";
    }

    class_loader::impl::printDebugInfoToScreen();

    auto factory = loader.createInstance<rclcpp_components::NodeFactory>(class_name);
    std::cout << "create_instance=ok\n";
    (void)factory;
    return 0;
  } catch (const std::exception & ex) {
    std::cerr << "create_instance_exception=" << ex.what() << "\n";
    class_loader::impl::printDebugInfoToScreen();
    return 1;
  }
}
