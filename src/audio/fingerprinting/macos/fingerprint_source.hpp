#pragma once

#include <filesystem>
#include <sys/stat.h>

namespace playset::detail {

struct FingerprintSourceVersion {
  std::filesystem::path path;
  struct stat attributes{};
  void verify_unchanged() const;
};

class FingerprintSource {
public:
  explicit FingerprintSource(const std::filesystem::path &path);
  ~FingerprintSource();
  FingerprintSource(const FingerprintSource &) = delete;
  FingerprintSource &operator=(const FingerprintSource &) = delete;
  [[nodiscard]] const std::filesystem::path &path() const noexcept { return path_; }
  [[nodiscard]] FingerprintSourceVersion version() const { return {path_, initial_}; }
  void verify_unchanged() const;

private:
  std::filesystem::path path_;
  int descriptor_ = -1;
  struct stat initial_{};
};

} // namespace playset::detail
