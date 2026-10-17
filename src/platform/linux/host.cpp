#include "host.hpp"

#include "text.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace {

bool isMount(const std::string& path) {
  if (path == "/") return true;
  struct stat self;
  struct stat parent;
  if (stat(path.c_str(), &self) != 0) return false;
  if (stat((path + "/..").c_str(), &parent) != 0) return false;
  return self.st_dev != parent.st_dev;
}

void addVolume(std::vector<VolumeEntry>& out, const std::string& path, const std::string& label) {
  struct statvfs vfs;
  VolumeEntry entry;
  entry.path = path;
  entry.label = label.empty() ? path : label;
  if (statvfs(path.c_str(), &vfs) == 0) {
    entry.total = static_cast<uint64_t>(vfs.f_blocks) * static_cast<uint64_t>(vfs.f_frsize);
    entry.free = static_cast<uint64_t>(vfs.f_bavail) * static_cast<uint64_t>(vfs.f_frsize);
  }
  for (const VolumeEntry& existing : out) {
    if (existing.path == entry.path) return;
  }
  out.push_back(entry);
}

void addMounts(std::vector<VolumeEntry>& out, const std::string& directory) {
  DIR* dir = opendir(directory.c_str());
  if (!dir) return;
  while (dirent* entry = readdir(dir)) {
    if (entry->d_name[0] == '.') continue;
    std::string path = directory;
    if (path.back() != '/') path.push_back('/');
    path += entry->d_name;
    struct stat st;
    if (stat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) continue;
    if (isMount(path)) addVolume(out, path, entry->d_name);
  }
  closedir(dir);
}

std::string runCapture(const char* command) {
  FILE* pipe = popen(command, "r");
  if (!pipe) return {};
  std::string out;
  char buffer[512];
  while (std::fgets(buffer, sizeof(buffer), pipe)) out += buffer;
  int code = pclose(pipe);
  if (code != 0) return {};
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
  return out;
}

}  // namespace

std::vector<VolumeEntry> linuxListVolumes() {
  std::vector<VolumeEntry> out;
  addVolume(out, "/", zh::rootFolder());
  if (const char* home = std::getenv("HOME")) addVolume(out, home, zh::homeFolder());
  addMounts(out, "/mnt");
  addMounts(out, "/media");
  addMounts(out, "/run/media");
  addMounts(out, "/Volumes");
  return out;
}

bool linuxPickPath(std::string& path, bool save) {
  std::string command = save ? "zenity --file-selection --save --confirm-overwrite --filename=usage.csv 2>/dev/null"
                             : "zenity --file-selection --directory 2>/dev/null";
  path = runCapture(command.c_str());
  if (!path.empty()) return true;
  command = save ? "kdialog --getsavefilename \"$HOME/usage.csv\" 2>/dev/null"
                 : "kdialog --getexistingdirectory \"$HOME\" 2>/dev/null";
  path = runCapture(command.c_str());
  return !path.empty();
}

void linuxLaunch(const std::string& program, const std::string& arg) {
  pid_t pid = fork();
  if (pid == 0) {
    execlp(program.c_str(), program.c_str(), arg.c_str(), static_cast<char*>(nullptr));
    _exit(127);
  }
}

void linuxFillHost(Host& host) {
  host.volumes = []() { return linuxListVolumes(); };
  host.pickFolder = [](std::string& path) { return linuxPickPath(path, false); };
  host.pickSave = [](std::string& path) {
    if (linuxPickPath(path, true)) return true;
    const char* home = std::getenv("HOME");
    path = home ? std::string(home) + "/SpaceViewer.csv" : "SpaceViewer.csv";
    return true;
  };
  host.openPath = [](const std::string& path) { linuxLaunch("xdg-open", path); };
  host.revealPath = [](const std::string& path) {
    std::string dir = path;
    auto slash = dir.find_last_of('/');
    if (slash != std::string::npos && slash > 0) dir.resize(slash);
    linuxLaunch("xdg-open", dir.empty() ? path : dir);
  };
}
