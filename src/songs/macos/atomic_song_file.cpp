#include "atomic_song_file.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>

namespace playset::detail {
namespace {
[[noreturn]] void failure(const char *operation, int error = errno) {
  throw std::system_error(error, std::generic_category(), operation);
}

class TemporarySongFile {
public:
  explicit TemporarySongFile(const std::filesystem::path &parent)
      : path_((parent / ".playset-song-XXXXXX.tmp").string()) {
    descriptor_ = ::mkstemps(path_.data(), 4);
    if (descriptor_ < 0) failure("Could not create a temporary song file");
  }
  ~TemporarySongFile() {
    if (descriptor_ >= 0) ::close(descriptor_);
    if (!committed_) ::unlink(path_.c_str());
  }
  TemporarySongFile(const TemporarySongFile &) = delete;
  TemporarySongFile &operator=(const TemporarySongFile &) = delete;

  void write(std::string_view text, mode_t mode) {
    if (::fcntl(descriptor_, F_SETFD, FD_CLOEXEC) != 0)
      failure("Could not prepare the temporary song file");
    if (::fchmod(descriptor_, mode) != 0) failure("Could not set song file permissions");
    while (!text.empty()) {
      const auto count = std::min(text.size(), static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
      const auto written = ::write(descriptor_, text.data(), count);
      if (written < 0 && errno == EINTR) continue;
      if (written < 0) failure("Could not write the temporary song file");
      if (written == 0) failure("Temporary song write made no progress", EIO);
      text.remove_prefix(static_cast<std::size_t>(written));
    }
    while (::fsync(descriptor_) != 0) {
      if (errno != EINTR) failure("Could not flush the temporary song file");
    }
    const auto descriptor = descriptor_;
    descriptor_ = -1;
    if (::close(descriptor) != 0) failure("Could not close the temporary song file");
  }

  void commit(const std::filesystem::path &destination) {
    if (::rename(path_.c_str(), destination.c_str()) != 0)
      failure("Could not replace the song file");
    committed_ = true;
  }

private:
  std::string path_;
  int descriptor_ = -1;
  bool committed_ = false;
};
}

void write_song_file_atomically(const std::filesystem::path &file, std::string_view text) {
  const auto parent = std::filesystem::canonical(file.has_parent_path() ? file.parent_path() : ".");
  if (!std::filesystem::is_directory(parent))
    throw std::invalid_argument("The song destination parent must be a directory.");
  const auto destination = parent / file.filename();
  mode_t mode = S_IRUSR | S_IWUSR;
  struct stat existing{};
  if (::lstat(destination.c_str(), &existing) == 0) {
    if (!S_ISREG(existing.st_mode))
      throw std::invalid_argument("An existing song destination must be a regular file, not a link or directory.");
    mode = existing.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO);
  } else if (errno != ENOENT) {
    failure("Could not inspect the song destination");
  }

  TemporarySongFile temporary(parent);
  temporary.write(text, mode);
  temporary.commit(destination);
}

} // namespace playset::detail
