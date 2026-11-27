#include "host.hpp"

#include "i18n/text.hpp"
#include "platform/posix/volume.hpp"

#include <cstdio>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include <dirent.h>
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using spaceviewer::posix::addVolume;

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
    if (spaceviewer::posix::isVolumeRoot(path)) addVolume(out, path, entry->d_name);
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

bool prepareDesktopChild() {
  if (geteuid() == 0) {
    uid_t uid = 0;
    for (const char* name : {"SPACEVIEWER_DESKTOP_UID", "PKEXEC_UID", "SUDO_UID"}) {
      const char* value = std::getenv(name);
      if (!value || !*value) continue;
      char* end = nullptr;
      errno = 0;
      const unsigned long parsed = std::strtoul(value, &end, 10);
      if (!errno && end != value && !*end && parsed > 0 && parsed <= std::numeric_limits<uid_t>::max()) {
        uid = static_cast<uid_t>(parsed);
        break;
      }
    }
    if (!uid) {
      struct stat runtime{};
      const char* path = std::getenv("XDG_RUNTIME_DIR");
      if (path && stat(path, &runtime) == 0 && S_ISDIR(runtime.st_mode)) uid = runtime.st_uid;
    }
    const passwd* user = uid ? getpwuid(uid) : nullptr;
    if (!user) {
      std::fprintf(stderr, "SpaceViewer: cannot identify the desktop user for opening files\n");
      return false;
    }
    const std::string name = user->pw_name, home = user->pw_dir;
    const gid_t gid = user->pw_gid;
    // Only the detached desktop launcher drops privileges; the scanner remains root.
    if (initgroups(name.c_str(), gid) != 0 || setgid(gid) != 0 || setuid(uid) != 0) {
      std::fprintf(stderr, "SpaceViewer: cannot switch to desktop user: %s\n", std::strerror(errno));
      return false;
    }
    setenv("HOME", home.c_str(), 1);
    setenv("USER", name.c_str(), 1);
    setenv("LOGNAME", name.c_str(), 1);
    const std::string runtime = "/run/user/" + std::to_string(uid);
    setenv("XDG_RUNTIME_DIR", runtime.c_str(), 1);
    if (!std::getenv("DBUS_SESSION_BUS_ADDRESS")) {
      const std::string bus = "unix:path=" + runtime + "/bus";
      setenv("DBUS_SESSION_BUS_ADDRESS", bus.c_str(), 1);
    }
  }
  // AppImage libraries belong to this application, not to desktop applications.
  if (std::getenv("APPIMAGE") || std::getenv("APPDIR")) unsetenv("LD_LIBRARY_PATH");
  return true;
}

void revealInFiles(const std::string& path) {
  std::error_code error;
  const auto absolute = std::filesystem::absolute(path, error);
  if (error) return;
  std::string uri = "file://";
  const char hex[] = "0123456789ABCDEF";
  for (unsigned char c : absolute.string()) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == '/' || c == '-' || c == '_' || c == '.' || c == '~') uri += static_cast<char>(c);
    else { uri += '%'; uri += hex[c >> 4]; uri += hex[c & 15]; }
  }
  const std::string items = "['" + uri + "']";
  const std::string parent = absolute.parent_path().string();
  pid_t launcher = fork();
  if (launcher == 0) {
    pid_t detached = fork();
    if (detached < 0) _exit(127);
    if (detached > 0) _exit(0);
    setsid();
    if (!prepareDesktopChild()) _exit(126);
    pid_t request = fork();
    if (request == 0) {
      execlp("gdbus", "gdbus", "call", "--session", "--timeout", "5",
             "--dest", "org.freedesktop.FileManager1", "--object-path", "/org/freedesktop/FileManager1",
             "--method", "org.freedesktop.FileManager1.ShowItems", items.c_str(), "",
             static_cast<char*>(nullptr));
      _exit(127);
    }
    int status = 0;
    pid_t result = -1;
    if (request > 0) {
      do { result = waitpid(request, &status, 0); } while (result < 0 && errno == EINTR);
    }
    if (result > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0) _exit(0);
    execlp("xdg-open", "xdg-open", parent.empty() ? "/" : parent.c_str(), static_cast<char*>(nullptr));
    _exit(127);
  }
  if (launcher > 0) while (waitpid(launcher, nullptr, 0) < 0 && errno == EINTR) {}
}

}  // namespace

void linuxIntegrateAppImage() {
  const char* imageEnv = std::getenv("APPIMAGE");
  const char* appDirEnv = std::getenv("APPDIR");
  const char* homeEnv = std::getenv("HOME");
  // Elevated root scans must never create launchers under /root.
  if (!imageEnv || !*imageEnv || !appDirEnv || !*appDirEnv || !homeEnv || !*homeEnv || geteuid() == 0) return;

  std::error_code error;
  const auto image = std::filesystem::canonical(imageEnv, error);
  if (error || !std::filesystem::is_regular_file(image, error)) return;
  const auto appDir = std::filesystem::path(appDirEnv);
  const auto sourceDesktop = appDir / "spaceviewer.desktop";
  const auto sourceIcon = appDir / "spaceviewer.png";
  const auto sourceThumbnailer = appDir / "usr/bin/spaceviewer-appimage-thumbnailer";
  if (!std::filesystem::is_regular_file(sourceDesktop, error) || error ||
      !std::filesystem::is_regular_file(sourceIcon, error) || error ||
      !std::filesystem::is_regular_file(sourceThumbnailer, error) || error) return;

  const auto home = std::filesystem::path(homeEnv);
  const auto applications = home / ".local/share/applications";
  const auto icons = home / ".local/share/icons/spaceviewer";
  const auto thumbnailers = home / ".local/share/thumbnailers";
  const auto libexec = home / ".local/libexec";
  const auto desktopPath = applications / "spaceviewer.desktop";
  const auto iconPath = icons / "spaceviewer.png";
  const auto thumbnailerPath = thumbnailers / "spaceviewer-appimage.thumbnailer";
  const auto helperPath = libexec / "spaceviewer-appimage-thumbnailer";
  std::filesystem::create_directories(applications, error);
  if (error) return;
  std::filesystem::create_directories(icons, error);
  if (error) return;
  std::filesystem::create_directories(thumbnailers, error);
  if (error) return;
  std::filesystem::create_directories(libexec, error);
  if (error) return;

  std::filesystem::copy_file(sourceIcon, iconPath, std::filesystem::copy_options::overwrite_existing, error);
  if (error) return;
  std::filesystem::copy_file(sourceThumbnailer, helperPath, std::filesystem::copy_options::overwrite_existing, error);
  if (error || chmod(helperPath.c_str(), 0700) != 0) return;

  {
    std::ofstream thumbnailer(thumbnailerPath, std::ios::trunc);
    thumbnailer << "[Thumbnailer Entry]\nTryExec=" << helperPath.string()
                << "\nExec=" << helperPath.string() << " %i %o"
                << "\nMimeType=application/vnd.appimage;application/x-iso9660-appimage;\n";
    if (!thumbnailer) return;
  }

  auto quoteExec = [](const std::string& value) {
    std::string quoted = "\"";
    for (char ch : value) {
      if (ch == '\\' || ch == '"' || ch == '`' || ch == '$') quoted.push_back('\\');
      if (ch == '%') quoted.push_back('%');
      quoted.push_back(ch);
    }
    quoted.push_back('"');
    return quoted;
  };
  std::ifstream input(sourceDesktop);
  std::ostringstream contents;
  std::string line;
  bool hasClass = false;
  while (std::getline(input, line)) {
    if (line.rfind("Exec=", 0) == 0) line = "Exec=" + quoteExec(image.string()) + " %F";
    else if (line.rfind("Icon=", 0) == 0) line = "Icon=" + iconPath.string();
    else if (line.rfind("StartupWMClass=", 0) == 0) {
      line = "StartupWMClass=spaceviewer";
      hasClass = true;
    }
    contents << line << '\n';
  }
  if (!input.eof()) return;
  if (!hasClass) contents << "StartupWMClass=spaceviewer\n";

  const auto temporary = desktopPath.string() + ".tmp";
  {
    std::ofstream output(temporary, std::ios::trunc);
    output << contents.str();
    if (!output) {
      std::filesystem::remove(temporary, error);
      return;
    }
  }
  std::filesystem::rename(temporary, desktopPath, error);
  if (error) std::filesystem::remove(temporary, error);
}

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
    pid_t child = fork();
    if (child < 0) _exit(127);
    if (child > 0) _exit(0);
    setsid();
    if (!prepareDesktopChild()) _exit(126);
    execlp(program.c_str(), program.c_str(), arg.c_str(), static_cast<char*>(nullptr));
    _exit(127);
  }
  if (pid > 0) {
    while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
  }
}

bool linuxLaunchRootScan() {
  const char* pkexec = std::getenv("SPACEVIEWER_PKEXEC");
  if (!pkexec || !*pkexec) pkexec = "pkexec";
  const char* appImage = std::getenv("APPIMAGE");
  char executable[4096];
  ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
  if (length <= 0) return false;
  executable[length] = '\0';

  std::vector<std::string> args{pkexec, "env", "APPIMAGE_EXTRACT_AND_RUN=1"};
  if (getuid() != 0) args.emplace_back("SPACEVIEWER_DESKTOP_UID=" + std::to_string(getuid()));
  const char* envNames[] = {"DISPLAY", "WAYLAND_DISPLAY", "XAUTHORITY", "XDG_RUNTIME_DIR",
                            "DBUS_SESSION_BUS_ADDRESS", "SPACEVIEWER_BACKEND"};
  for (const char* name : envNames) {
    if (const char* value = std::getenv(name)) args.emplace_back(std::string(name) + "=" + value);
  }
  if (appImage && *appImage) args.emplace_back(std::string("APPIMAGE=") + appImage);
  args.emplace_back(appImage && *appImage ? appImage : executable);
  args.emplace_back("/");

  pid_t pid = fork();
  if (pid < 0) return false;
  if (pid == 0) {
    pid_t child = fork();
    if (child < 0) _exit(127);
    if (child > 0) _exit(0);
    setsid();
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (std::string& arg : args) argv.push_back(arg.data());
    argv.push_back(nullptr);
    execvp(argv[0], argv.data());
    _exit(127);
  }
  while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
  return true;
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
  host.revealPath = [](const std::string& path) { revealInFiles(path); };
}
