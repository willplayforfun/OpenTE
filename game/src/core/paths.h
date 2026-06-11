#pragma once

#include <filesystem>
#include <optional>

namespace opente::core {

/// Locates the `game_data/` directory produced by the OpenTE extractor.
///
/// The extractor writes its output to a `game_data/` directory next to the
/// extractor (and, by convention, next to the game executable when both are
/// distributed together in a release). During development the build output
/// directory and the source tree are different places, so this searches a
/// short list of likely locations:
///   1. next to the running executable
///   2. the executable's parent directory (covers `build/<preset>/bin/`)
///   3. the current working directory
///
/// Returns std::nullopt if no `game_data/` directory is found in any of
/// those locations.
std::optional<std::filesystem::path> find_game_data_dir(
    const std::filesystem::path& executable_path);

}  // namespace opente::core
