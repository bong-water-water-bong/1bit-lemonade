#include "lemon/utils/path_utils.h"

namespace lemon {
namespace utils {

std::filesystem::path path_from_utf8(const std::string& path) {
#ifdef _WIN32
    return std::filesystem::u8path(path);
#else
    return std::filesystem::path(path);
#endif
}

} // namespace utils
} // namespace lemon
