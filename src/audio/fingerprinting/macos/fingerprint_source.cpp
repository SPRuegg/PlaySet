#include "fingerprint_source.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <stdexcept>
#include <system_error>

namespace playset::detail {
namespace {
bool same_version(const struct stat &a, const struct stat &b) {
  return S_ISREG(b.st_mode) && a.st_dev == b.st_dev && a.st_ino == b.st_ino &&
      a.st_size == b.st_size && a.st_mtimespec.tv_sec == b.st_mtimespec.tv_sec &&
      a.st_mtimespec.tv_nsec == b.st_mtimespec.tv_nsec &&
      a.st_ctimespec.tv_sec == b.st_ctimespec.tv_sec && a.st_ctimespec.tv_nsec == b.st_ctimespec.tv_nsec;
}
}

FingerprintSource::FingerprintSource(const std::filesystem::path &path)
    : path_(std::filesystem::canonical(path)) {
  descriptor_ = ::open(path_.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
  if (descriptor_ < 0)
    throw std::system_error(errno, std::generic_category(), "Could not open fingerprint source: " + path_.string());
  if (::fstat(descriptor_, &initial_) != 0) {
    const auto error = errno;
    ::close(descriptor_);
    throw std::system_error(error, std::generic_category(), "Could not inspect fingerprint source: " + path_.string());
  }
  if (!S_ISREG(initial_.st_mode)) {
    ::close(descriptor_);
    throw std::runtime_error("Fingerprint source must be a regular file: " + path_.string());
  }
}
FingerprintSource::~FingerprintSource() { ::close(descriptor_); }

void FingerprintSourceVersion::verify_unchanged() const {
  struct stat current{};
  if (::stat(path.c_str(), &current) != 0 || !same_version(attributes, current))
    throw std::runtime_error("Source audio changed during fingerprinting: " + path.string());
}

void FingerprintSource::verify_unchanged() const {
  struct stat opened{}, current{};
  if (::fstat(descriptor_, &opened) != 0 || ::stat(path_.c_str(), &current) != 0 ||
      !same_version(initial_, opened) || !same_version(initial_, current))
    throw std::runtime_error("Source audio changed during fingerprinting: " + path_.string());
}

} // namespace playset::detail
