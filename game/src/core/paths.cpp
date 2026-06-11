#include "core/paths.h"

namespace opente::core {

std::optional<std::filesystem::path> find_game_data_dir(
    const std::filesystem::path& executable_path) {
    namespace fs = std::filesystem;

    const fs::path exe_dir = fs::absolute(executable_path).parent_path();

    const fs::path candidates[] = {
        exe_dir / "game_data",
        exe_dir.parent_path() / "game_data",
        fs::current_path() / "game_data",
    };

    for (const fs::path& candidate : candidates) {
        if (fs::is_directory(candidate)) {
            return candidate;
        }
    }

    return std::nullopt;
}

}  // namespace opente::core
