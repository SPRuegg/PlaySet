#include "songs/song_file.hpp"
#include "fixture_support.hpp"

#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace {
using namespace playset;
using namespace playset::test;

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
template <class F> void rejects(F action) {
  try { action(); } catch (const std::exception &) { return; }
  throw std::runtime_error("Invalid save was accepted");
}

SongConfiguration configuration() {
  SongConfiguration song;
  song.id = "saved-song";
  song.stem_folder = "/Music/Example Song/MultiTracks";
  song.fingerprint.stem_count = 1;
  song.tracks = {{"track-3", "Bass.wav", StemFingerprint{}, 0.5, true, true}};
  song.master = {0.75, true};
  return song;
}

std::string read(const fs::path &file) {
  std::ifstream input(file, std::ios::binary);
  require(input.good(), "Could not inspect saved song");
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void no_temporary_files(const fs::path &folder) {
  for (const auto &entry : fs::directory_iterator(folder))
    require(!entry.path().filename().string().starts_with(".playset-song-"),
            "Save left its temporary file behind");
}

mode_t mode(const fs::path &file) {
  struct stat attributes{};
  require(::stat(file.c_str(), &attributes) == 0, "Could not inspect song permissions");
  return attributes.st_mode & 0777;
}

void saving() {
  TempFolder folder;
  const auto file = folder.path / "Example Song.plsong";
  auto song = configuration();
  const auto expected = serialize_song_configuration(song);
  save_song_configuration(file, song);
  require(read(file) == expected && mode(file) == 0600,
          "New save changed its JSON or did not create a private file");
  require(song.tracks[0].volume == 0.5 && song.master.muted, "Saving changed the supplied settings");
  no_temporary_files(folder.path);

  fs::permissions(file, fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read);
  const auto previous_inode = [&] {
    struct stat attributes{};
    require(::stat(file.c_str(), &attributes) == 0, "Could not inspect the original file");
    return attributes.st_ino;
  }();
  song.tracks[0].volume = 0.25;
  song.master = {0.125, false};
  save_song_configuration(file, song);
  struct stat updated{};
  require(::stat(file.c_str(), &updated) == 0 && updated.st_ino != previous_inode,
          "Save modified the original inode instead of replacing the directory entry");
  require(read(file) == serialize_song_configuration(song) && mode(file) == 0640,
          "Overwrite lost updated settings or basic file permissions");
  no_temporary_files(folder.path);

  const auto pending = folder.path / ".playset-song-existing.tmp";
  { std::ofstream output(pending); output << "unrelated interrupted save"; }
  save_song_configuration(file, song);
  require(read(pending) == "unrelated interrupted save", "Save deleted someone else's temporary file");
}

void destinations() {
  TempFolder folder;
  const auto song = configuration();
  const auto uppercase = folder.path / "Uppercase.PLSONG";
  save_song_configuration(uppercase, song);
  require(read(uppercase) == serialize_song_configuration(song), "Uppercase extension was rejected");
  const auto relative = fs::relative(folder.path / "Relative.plsong", fs::current_path());
  save_song_configuration(relative, song);
  require(read(relative) == serialize_song_configuration(song), "Relative destination was not saved");
  fs::create_directory_symlink(folder.path, folder.path / "folder-link");
  save_song_configuration(folder.path / "folder-link" / "Linked Folder.plsong", song);
  require(read(folder.path / "Linked Folder.plsong") == serialize_song_configuration(song),
          "Parent folder alias did not resolve to the chosen folder");

  const auto link = folder.path / "link.plsong";
  fs::create_symlink(uppercase, link);
  rejects([&] { save_song_configuration(link, song); });
  require(fs::is_symlink(link) && read(uppercase) == serialize_song_configuration(song),
          "Rejected symlink save changed the link or its target");
  const auto dangling = folder.path / "dangling.plsong";
  fs::create_symlink(folder.path / "missing.plsong", dangling);
  rejects([&] { save_song_configuration(dangling, song); });
  require(fs::is_symlink(dangling) && !fs::exists(folder.path / "missing.plsong"),
          "Rejected dangling link save created its target");

  const auto directory = folder.path / "directory.plsong";
  fs::create_directory(directory);
  { std::ofstream marker(directory / "keep"); marker << "preserved"; }
  rejects([&] { save_song_configuration(directory, song); });
  require(read(directory / "keep") == "preserved", "Save changed a rejected destination directory");
  for (const auto &file : {fs::path{}, folder.path / "wrong.wav", folder.path / "missing" / "song.plsong"})
    rejects([&] { save_song_configuration(file, song); });
  rejects([&] { save_song_configuration(folder.path / std::string("bad\0.plsong", 11), song); });
  rejects([&] { save_song_configuration(uppercase / "nested.plsong", song); });
  no_temporary_files(folder.path);
}

void failures() {
  TempFolder folder;
  const auto file = folder.path / "Protected.plsong";
  const auto song = configuration();
  save_song_configuration(file, song);
  const auto original = read(file);
  auto invalid = song;
  invalid.master.volume = std::numeric_limits<double>::quiet_NaN();
  rejects([&] { save_song_configuration(file, invalid); });
  require(read(file) == original, "Validation failure changed an existing file");
  rejects([&] { save_song_configuration(folder.path / "new.plsong", invalid); });
  require(!fs::exists(folder.path / "new.plsong"), "Validation failure created a destination");
  no_temporary_files(folder.path);

  const auto child = ::fork();
  require(child >= 0, "Could not start the file-limit test");
  if (child == 0) {
    const struct rlimit limit{128, 128};
    if (std::signal(SIGXFSZ, SIG_IGN) == SIG_ERR || ::setrlimit(RLIMIT_FSIZE, &limit) != 0)
      ::_exit(2);
    for (const auto &destination : {file, folder.path / "partial.plsong"}) {
      try { save_song_configuration(destination, song); }
      catch (const std::system_error &) { continue; }
      catch (...) { ::_exit(3); }
      ::_exit(4);
    }
    ::_exit(0);
  }
  int status = 0;
  while (::waitpid(child, &status, 0) < 0) {
    if (errno != EINTR) throw std::runtime_error("Could not wait for the file-limit test");
  }
  require(WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "A partial write was not reported as a save failure");
  require(read(file) == original, "Partial write failure changed the existing destination");
  require(!fs::exists(folder.path / "partial.plsong"), "Partial write failure created a destination");
  no_temporary_files(folder.path);

  if (::geteuid() != 0) {
    fs::permissions(folder.path, fs::perms::owner_read | fs::perms::owner_exec);
    bool rejected = false;
    try { save_song_configuration(file, song); } catch (const std::exception &) { rejected = true; }
    fs::permissions(folder.path, fs::perms::owner_all);
    require(rejected && read(file) == original, "Unwritable folder did not preserve its original file");
    no_temporary_files(folder.path);
  }
}
}

int main(int argc, char *argv[]) {
  try {
    if (argc != 2) throw std::runtime_error("Supply a song file test case");
    const std::string_view test(argv[1]);
    if (test == "saving") saving();
    else if (test == "destinations") destinations();
    else if (test == "failures") failures();
    else throw std::runtime_error("Unknown song file test case");
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
