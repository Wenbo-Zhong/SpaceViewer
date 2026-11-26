#include "model/category.hpp"
#include "i18n/unicode.hpp"

#include <string>
#include <unordered_set>

namespace {

const std::unordered_set<std::string>& extSet(Category cat) {
  static const std::unordered_set<std::string> image = {
      "jpg", "jpeg", "png", "gif", "bmp", "webp", "tif", "tiff", "heic", "ico", "psd", "svg"};
  static const std::unordered_set<std::string> video = {
      "mp4", "mkv", "avi", "mov", "wmv", "webm", "flv", "m4v", "mpg", "mpeg", "ts"};
  static const std::unordered_set<std::string> audio = {
      "mp3", "wav", "flac", "aac", "m4a", "ogg", "wma", "aiff"};
  static const std::unordered_set<std::string> archive = {
      "zip", "rar", "7z", "tar", "gz", "bz2", "xz", "iso", "cab", "tgz"};
  static const std::unordered_set<std::string> doc = {
      "pdf", "doc", "docx", "xls", "xlsx", "ppt", "pptx", "txt", "rtf", "md", "csv"};
  static const std::unordered_set<std::string> code = {
      "c", "h", "cpp", "hpp", "cc", "hh", "rs", "py", "js", "ts", "tsx", "jsx", "java",
      "go", "php", "rb", "cs", "swift", "kt", "json", "xml", "yml", "yaml", "html", "css"};
  static const std::unordered_set<std::string> exe = {
      "exe", "dll", "msi", "sys", "so", "dylib", "bin", "app", "apk"};
  switch (cat) {
    case CatImage: return image;
    case CatVideo: return video;
    case CatAudio: return audio;
    case CatArchive: return archive;
    case CatDoc: return doc;
    case CatCode: return code;
    case CatExe: return exe;
    default: break;
  }
  static const std::unordered_set<std::string> empty;
  return empty;
}

}  // namespace

uint8_t categoryFromName(std::string_view name) {
  auto dot = name.rfind('.');
  if (dot == std::string_view::npos || dot == 0 || dot + 1 >= name.size()) return CatOther;
  if (name.size() - dot > 8) return CatOther;
  std::string ext;
  ext.reserve(name.size() - dot - 1);
  for (size_t i = dot + 1; i < name.size(); ++i) ext.push_back(lowerAscii(name[i]));
  if (extSet(CatImage).count(ext)) return CatImage;
  if (extSet(CatVideo).count(ext)) return CatVideo;
  if (extSet(CatAudio).count(ext)) return CatAudio;
  if (extSet(CatArchive).count(ext)) return CatArchive;
  if (extSet(CatDoc).count(ext)) return CatDoc;
  if (extSet(CatCode).count(ext)) return CatCode;
  if (extSet(CatExe).count(ext)) return CatExe;
  return CatOther;
}
