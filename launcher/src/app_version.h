#pragma once

#include <filesystem>
#include <string>

// The version of this copy of the game: the narutorise.version file that the
// release package and the installer write next to the executables, or else the
// version this launcher was built with (packaging/version.iss). Empty when
// neither is known.
std::string AppVersion(const std::filesystem::path& base_dir);
