#pragma once

#include <filesystem>
#include <sys/stat.h>

namespace playset::detail {

class FingerprintSource {
public:
  explicit FingerprintSource(const std::filesystem::path &path);
  ~FingerprintSource();
  FingerprintSource(const FingerprintSource &) = delete;
  FingerprintSource &operator=(const FingerprintSource &) = delete;
  [[nodiscard]] const std::filesystem::path &path() const noexcept { return path_; }
  void verify_unchanged() const;

private:
  std::filesystem::path path_;
  int descriptor_ = -1;
  struct stat initial_{};
};

} // namespace playset::detail
