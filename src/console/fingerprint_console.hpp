#pragma once

#include <filesystem>
#include <iosfwd>

namespace playset {

void inspect_stem_fingerprints(const std::filesystem::path &folder, std::ostream &output);

} // namespace playset
