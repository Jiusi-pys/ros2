#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "class_loader/class_loader.hpp"
#include "class_loader/class_loader_core.hpp"
#include "rosbag2_storage/storage_interfaces/read_write_interface.hpp"
#include "rosbag2_storage/storage_options.hpp"

int main(int argc, char * argv[])
{
  if (argc != 3 && argc != 4) {
    std::cerr << "Usage: " << argv[0] << " <library-path> <class-name> [uri]\n";
    return 2;
  }

  const std::string library_path = argv[1];
  const std::string class_name = argv[2];
  const std::string uri = (argc == 4) ? argv[3] : "";

  try {
    class_loader::ClassLoader loader(library_path);

    const auto classes =
      loader.getAvailableClasses<rosbag2_storage::storage_interfaces::ReadWriteInterface>();
    std::cout << "declared_classes=" << classes.size() << "\n";
    for (const auto & entry : classes) {
      std::cout << "  " << entry << "\n";
    }

    auto instance = std::shared_ptr<rosbag2_storage::storage_interfaces::ReadWriteInterface>(
      loader.createUnmanagedInstance<rosbag2_storage::storage_interfaces::ReadWriteInterface>(
        class_name));
    std::cout << "create_instance=ok\n";

    if (!uri.empty()) {
      rosbag2_storage::StorageOptions options;
      options.uri = uri;
      options.storage_id = class_name;
      instance->open(options);
      std::cout << "open=ok\n";
    }
    return 0;
  } catch (const std::exception & ex) {
    std::cerr << "probe_exception=" << ex.what() << "\n";
    class_loader::impl::printDebugInfoToScreen();
    return 1;
  }
}
