#pragma once

// UI copy is written with universal character names so MSVC does not depend on
// the source code page (Chinese Windows defaults to GBK).

namespace zh {

inline const char* open() { return "\u6253\u5f00"; }
inline const char* drives() { return "\u78c1\u76d8"; }
inline const char* back() { return "\u540e\u9000"; }
inline const char* forward() { return "\u524d\u8fdb"; }
inline const char* up() { return "\u5411\u4e0a"; }
inline const char* rescan() { return "\u91cd\u65b0\u626b\u63cf"; }
inline const char* stop() { return "\u505c\u6b62"; }
inline const char* stopping() { return "\u6b63\u5728\u505c\u6b62"; }
inline const char* exportCsv() { return "\u5bfc\u51fa"; }
inline const char* include() { return "\u5305\u542b"; }
inline const char* exclude() { return "\u6392\u9664"; }
inline const char* minSize() { return "\u6700\u5c0f"; }
inline const char* freeSpace() { return "\u53ef\u7528\u7a7a\u95f4"; }
inline const char* uncounted() { return "\u672a\u7edf\u8ba1"; }
inline const char* scanningRegion() { return "\u6b63\u5728\u626b\u63cf"; }
inline const char* files() { return "\u6587\u4ef6"; }
inline const char* folders() { return "\u6587\u4ef6\u5939"; }
inline const char* used() { return "\u5df2\u7528"; }
inline const char* avail() { return "\u53ef\u7528"; }
inline const char* errors() { return "\u9519\u8bef"; }
inline const char* ready() { return "\u5c31\u7eea"; }
inline const char* scanning() { return "\u626b\u63cf\u4e2d"; }
inline const char* scanDone() { return "\u626b\u63cf\u5b8c\u6210"; }
inline const char* stopped() { return "\u5df2\u505c\u6b62"; }
inline const char* emptyHint() {
  return "\u9009\u62e9\u6587\u4ef6\u5939\u6216\u78c1\u76d8\u5f00\u59cb\u3002\u4e5f\u53ef\u4ee5\u628a\u6587\u4ef6\u5939\u62d6\u8fdb\u7a97\u53e3\uff0c\u6216\u5728\u8def\u5f84\u680f\u8f93\u5165\u540e\u6309\u56de\u8f66\u3002";
}
inline const char* emptyFolder() { return "\u6b64\u6587\u4ef6\u5939\u4e3a\u7a7a"; }
inline const char* cannotOpen() { return "\u65e0\u6cd5\u6253\u5f00\u8be5\u8def\u5f84"; }
inline const char* openLocation() { return "\u6253\u5f00\u6240\u5728\u4f4d\u7f6e"; }
inline const char* copyPath() { return "\u590d\u5236\u8def\u5f84"; }
inline const char* enterFolder() { return "\u8fdb\u5165\u6b64\u6587\u4ef6\u5939"; }
inline const char* noMatch() { return "\u6ca1\u6709\u5339\u914d\u7684\u9879\u76ee"; }
inline const char* ofView() { return "\u5360\u5f53\u524d\u89c6\u56fe"; }
inline const char* exportOk() { return "\u5bfc\u51fa\u5b8c\u6210"; }
inline const char* exportFail() { return "\u5bfc\u51fa\u5931\u8d25"; }
inline const char* exporting() { return "\u6b63\u5728\u5bfc\u51fa"; }
inline const char* fixedDisk() { return "\u672c\u5730\u78c1\u76d8"; }
inline const char* removableDisk() { return "\u53ef\u79fb\u52a8\u78c1\u76d8"; }
inline const char* networkDisk() { return "\u7f51\u7edc\u78c1\u76d8"; }
inline const char* opticalDisk() { return "\u5149\u76d8"; }
inline const char* homeFolder() { return "\u4e3b\u6587\u4ef6\u5939"; }
inline const char* rootFolder() { return "\u6839\u76ee\u5f55"; }
inline const char* copied() { return "\u5df2\u590d\u5236\u8def\u5f84"; }
inline const char* uncountedNote() {
  return "\u672a\u7edf\u8ba1\u7684\u7a7a\u95f4\u6765\u81ea\u6743\u9650\u3001\u7c07\u5927\u5c0f\u6216\u786c\u94fe\u63a5";
}
inline const char* mergedNote() { return "\u8f83\u5c0f\u7684\u9879\u7559\u5728\u6587\u4ef6\u5939\u91cc\uff0c\u8fdb\u5165\u540e\u53ef\u4ee5\u770b\u5230"; }
inline const char* watchGap() { return "\u53d8\u66f4\u592a\u591a\uff0c\u90e8\u5206\u589e\u5220\u53ef\u80fd\u6ca1\u8ddf\u4e0a\uff0c\u53ef\u6309 F5 \u91cd\u65b0\u626b\u63cf"; }
inline const char* pickTitle() { return "\u9009\u62e9\u8981\u626b\u63cf\u7684\u6587\u4ef6\u5939"; }
inline const char* saveTitle() { return "\u5bfc\u51fa CSV"; }
inline const char* csvType() { return "CSV"; }
inline const char* allFiles() { return "\u6240\u6709\u6587\u4ef6"; }
inline const char* perSec() { return "\u6587\u4ef6/\u79d2"; }
inline const char* fileType() { return "\u6587\u4ef6"; }
inline const char* dirType() { return "\u6587\u4ef6\u5939"; }
inline const char* invalidMin() { return "\u6700\u5c0f\u5927\u5c0f\u65e0\u6548"; }
inline const char* hintInclude() { return "*.jpg;*.zip"; }
inline const char* hintExclude() { return "*.tmp;~*"; }
inline const char* hintMin() { return "1M"; }
inline const char* colPath() { return "\u8def\u5f84"; }
inline const char* colName() { return "\u540d\u79f0"; }
inline const char* colType() { return "\u7c7b\u578b"; }
inline const char* colBytes() { return "\u5927\u5c0f\u5b57\u8282"; }
inline const char* colSize() { return "\u5927\u5c0f"; }
inline const char* colPercent() { return "\u5360\u6bd4"; }

}  // namespace zh
