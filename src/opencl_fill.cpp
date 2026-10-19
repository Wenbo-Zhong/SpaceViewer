#include "opencl_fill.hpp"

#include "util.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define CL_API __stdcall
#else
#include <dlfcn.h>
#define CL_API
#endif

namespace {

using cl_int = int32_t;
using cl_uint = uint32_t;
using cl_platform_id = struct _cl_platform_id*;
using cl_device_id = struct _cl_device_id*;
using cl_context = struct _cl_context*;
using cl_command_queue = struct _cl_command_queue*;
using cl_program = struct _cl_program*;
using cl_kernel = struct _cl_kernel*;
using cl_mem = struct _cl_mem*;

constexpr cl_int CL_SUCCESS = 0;
constexpr cl_uint CL_DEVICE_TYPE_GPU = 1u << 2;
constexpr cl_uint CL_DEVICE_TYPE_CPU = 1u << 1;
constexpr cl_uint CL_MEM_READ_ONLY = 1u << 2;
constexpr cl_uint CL_MEM_READ_WRITE = 1u << 0;
constexpr cl_uint CL_MEM_COPY_HOST_PTR = 1u << 5;
constexpr cl_uint CL_PROGRAM_BUILD_LOG = 0x1183;

struct ClTile {
  int x, y, w, h;
  int titleH;
  int divH;
  unsigned top, bot, title, divider;
};

struct Bin {
  int start;
  int count;
};

using PFN_clGetPlatformIDs = cl_int(CL_API*)(cl_uint, cl_platform_id*, cl_uint*);
using PFN_clGetDeviceIDs = cl_int(CL_API*)(cl_platform_id, cl_uint, cl_uint, cl_device_id*, cl_uint*);
using PFN_clCreateContext = cl_context(CL_API*)(const intptr_t*, cl_uint, const cl_device_id*, void*, void*, cl_int*);
using PFN_clCreateCommandQueue = cl_command_queue(CL_API*)(cl_context, cl_device_id, cl_uint, cl_int*);
using PFN_clCreateProgramWithSource = cl_program(CL_API*)(cl_context, cl_uint, const char**, const size_t*, cl_int*);
using PFN_clBuildProgram = cl_int(CL_API*)(cl_program, cl_uint, const cl_device_id*, const char*, void*, void*);
using PFN_clCreateKernel = cl_kernel(CL_API*)(cl_program, const char*, cl_int*);
using PFN_clCreateBuffer = cl_mem(CL_API*)(cl_context, cl_uint, size_t, void*, cl_int*);
using PFN_clSetKernelArg = cl_int(CL_API*)(cl_kernel, cl_uint, size_t, const void*);
using PFN_clEnqueueNDRangeKernel = cl_int(CL_API*)(cl_command_queue, cl_kernel, cl_uint, const size_t*, const size_t*,
                                                  const size_t*, cl_uint, const void*, void*);
using PFN_clEnqueueReadBuffer = cl_int(CL_API*)(cl_command_queue, cl_mem, cl_uint, size_t, size_t, void*, cl_uint,
                                                const void*, void*);
using PFN_clEnqueueWriteBuffer = cl_int(CL_API*)(cl_command_queue, cl_mem, cl_uint, size_t, size_t, const void*, cl_uint,
                                                 const void*, void*);
using PFN_clFinish = cl_int(CL_API*)(cl_command_queue);
using PFN_clReleaseMemObject = cl_int(CL_API*)(cl_mem);
using PFN_clGetProgramBuildInfo = cl_int(CL_API*)(cl_program, cl_device_id, cl_uint, size_t, void*, size_t*);

struct Api {
  PFN_clGetPlatformIDs getPlatforms = nullptr;
  PFN_clGetDeviceIDs getDevices = nullptr;
  PFN_clCreateContext createContext = nullptr;
  PFN_clCreateCommandQueue createQueue = nullptr;
  PFN_clCreateProgramWithSource createProgram = nullptr;
  PFN_clBuildProgram buildProgram = nullptr;
  PFN_clCreateKernel createKernel = nullptr;
  PFN_clCreateBuffer createBuffer = nullptr;
  PFN_clSetKernelArg setArg = nullptr;
  PFN_clEnqueueNDRangeKernel enqueue = nullptr;
  PFN_clEnqueueReadBuffer readBuffer = nullptr;
  PFN_clEnqueueWriteBuffer writeBuffer = nullptr;
  PFN_clFinish finish = nullptr;
  PFN_clReleaseMemObject releaseMem = nullptr;
  PFN_clGetProgramBuildInfo buildInfo = nullptr;
  cl_context context = nullptr;
  cl_command_queue queue = nullptr;
  cl_kernel kernel = nullptr;
  cl_mem pixels = nullptr;
  cl_mem tileBuf = nullptr;
  cl_mem indexBuf = nullptr;
  cl_mem binBuf = nullptr;
  size_t pixelBytes = 0;
  size_t tileBytes = 0;
  size_t indexBytes = 0;
  size_t binBytes = 0;
  bool ready = false;
  bool failed = false;
};

Api& api() {
  static Api value;
  return value;
}

void* loadSym(void* library, const char* name) {
#if defined(_WIN32)
  return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(library), name));
#else
  return dlsym(library, name);
#endif
}

const char* kSource = R"CL(
typedef struct {
  int x, y, w, h;
  int titleH;
  int divH;
  unsigned top, bot, title, divider;
} Tile;
typedef struct { int start; int count; } Bin;
inline unsigned mix(unsigned a, unsigned b, int t, int span) {
  int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255;
  int br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
  int r = ar + (br - ar) * t / span;
  int g = ag + (bg - ag) * t / span;
  int bl = ab + (bb - ab) * t / span;
  return 0xff000000u | (r << 16) | (g << 8) | bl;
}
__kernel void fill_tiles(__global unsigned* pixels, int width, int originX, int originY, int mapW, int mapH,
                         int binSize, int binsX, __global const Tile* tiles, __global const int* indices,
                         __global const Bin* bins) {
  int gx = get_global_id(0);
  int gy = get_global_id(1);
  if (gx >= mapW || gy >= mapH) return;
  int x = originX + gx;
  int y = originY + gy;
  int bx = gx / binSize;
  int by = gy / binSize;
  Bin bin = bins[by * binsX + bx];
  for (int n = bin.count - 1; n >= 0; --n) {
    Tile tile = tiles[indices[bin.start + n]];
    if (x < tile.x || y < tile.y || x >= tile.x + tile.w || y >= tile.y + tile.h) continue;
    int ly = y - tile.y;
    unsigned color;
    if (tile.titleH > 0 && ly < tile.titleH) {
      if (tile.divH > 0 && ly >= tile.titleH - tile.divH) color = tile.divider;
      else color = tile.title;
    } else if (tile.h <= 1) {
      color = tile.top;
    } else {
      int span = tile.h - 1;
      if (span < 1) span = 1;
      int t = ly;
      if (t < 0) t = 0;
      if (t > span) t = span;
      color = mix(tile.top, tile.bot, t, span);
    }
    pixels[y * width + x] = 0xff000000u | (color & 0x00ffffffu);
    return;
  }
}
)CL";

bool initApi() {
  Api& cl = api();
  if (cl.ready || cl.failed) return cl.ready;
  void* library = nullptr;
#if defined(_WIN32)
  library = LoadLibraryW(L"OpenCL.dll");
#elif defined(__APPLE__)
  library = dlopen("/System/Library/Frameworks/OpenCL.framework/OpenCL", RTLD_LAZY);
  if (!library) library = dlopen("libOpenCL.dylib", RTLD_LAZY);
#else
  library = dlopen("libOpenCL.so.1", RTLD_LAZY);
  if (!library) library = dlopen("libOpenCL.so", RTLD_LAZY);
#endif
  if (!library) {
    cl.failed = true;
    return false;
  }
  cl.getPlatforms = reinterpret_cast<PFN_clGetPlatformIDs>(loadSym(library, "clGetPlatformIDs"));
  cl.getDevices = reinterpret_cast<PFN_clGetDeviceIDs>(loadSym(library, "clGetDeviceIDs"));
  cl.createContext = reinterpret_cast<PFN_clCreateContext>(loadSym(library, "clCreateContext"));
  cl.createQueue = reinterpret_cast<PFN_clCreateCommandQueue>(loadSym(library, "clCreateCommandQueue"));
  cl.createProgram = reinterpret_cast<PFN_clCreateProgramWithSource>(loadSym(library, "clCreateProgramWithSource"));
  cl.buildProgram = reinterpret_cast<PFN_clBuildProgram>(loadSym(library, "clBuildProgram"));
  cl.createKernel = reinterpret_cast<PFN_clCreateKernel>(loadSym(library, "clCreateKernel"));
  cl.createBuffer = reinterpret_cast<PFN_clCreateBuffer>(loadSym(library, "clCreateBuffer"));
  cl.setArg = reinterpret_cast<PFN_clSetKernelArg>(loadSym(library, "clSetKernelArg"));
  cl.enqueue = reinterpret_cast<PFN_clEnqueueNDRangeKernel>(loadSym(library, "clEnqueueNDRangeKernel"));
  cl.readBuffer = reinterpret_cast<PFN_clEnqueueReadBuffer>(loadSym(library, "clEnqueueReadBuffer"));
  cl.writeBuffer = reinterpret_cast<PFN_clEnqueueWriteBuffer>(loadSym(library, "clEnqueueWriteBuffer"));
  cl.finish = reinterpret_cast<PFN_clFinish>(loadSym(library, "clFinish"));
  cl.releaseMem = reinterpret_cast<PFN_clReleaseMemObject>(loadSym(library, "clReleaseMemObject"));
  cl.buildInfo = reinterpret_cast<PFN_clGetProgramBuildInfo>(loadSym(library, "clGetProgramBuildInfo"));
  if (!cl.getPlatforms || !cl.getDevices || !cl.createContext || !cl.createQueue || !cl.createProgram ||
      !cl.buildProgram || !cl.createKernel || !cl.createBuffer || !cl.setArg || !cl.enqueue || !cl.readBuffer ||
      !cl.writeBuffer || !cl.finish || !cl.releaseMem) {
    cl.failed = true;
    return false;
  }
  cl_uint platforms = 0;
  if (cl.getPlatforms(0, nullptr, &platforms) != CL_SUCCESS || platforms == 0) {
    cl.failed = true;
    return false;
  }
  std::vector<cl_platform_id> platform(platforms);
  cl.getPlatforms(platforms, platform.data(), nullptr);
  cl_device_id device = nullptr;
  for (cl_platform_id id : platform) {
    cl_uint count = 0;
    if (cl.getDevices(id, CL_DEVICE_TYPE_GPU, 1, &device, &count) == CL_SUCCESS && count > 0) break;
    device = nullptr;
  }
  if (!device) {
    for (cl_platform_id id : platform) {
      cl_uint count = 0;
      if (cl.getDevices(id, CL_DEVICE_TYPE_CPU, 1, &device, &count) == CL_SUCCESS && count > 0) break;
      device = nullptr;
    }
  }
  if (!device) {
    cl.failed = true;
    return false;
  }
  cl_int err = 0;
  cl.context = cl.createContext(nullptr, 1, &device, nullptr, nullptr, &err);
  if (!cl.context || err != CL_SUCCESS) {
    cl.failed = true;
    return false;
  }
  cl.queue = cl.createQueue(cl.context, device, 0, &err);
  if (!cl.queue || err != CL_SUCCESS) {
    cl.failed = true;
    return false;
  }
  const char* source = kSource;
  size_t length = std::char_traits<char>::length(source);
  cl_program program = cl.createProgram(cl.context, 1, &source, &length, &err);
  if (!program || cl.buildProgram(program, 1, &device, "-cl-std=CL1.2", nullptr, nullptr) != CL_SUCCESS) {
    cl.failed = true;
    return false;
  }
  cl.kernel = cl.createKernel(program, "fill_tiles", &err);
  if (!cl.kernel || err != CL_SUCCESS) {
    cl.failed = true;
    return false;
  }
  cl.ready = true;
  return true;
}

cl_mem grow(cl_mem existing, size_t& capacity, size_t need, cl_uint flags) {
  Api& cl = api();
  if (existing && capacity >= need) return existing;
  if (existing) cl.releaseMem(existing);
  cl_int err = 0;
  cl_mem memory = cl.createBuffer(cl.context, flags, need, nullptr, &err);
  capacity = err == CL_SUCCESS ? need : 0;
  return err == CL_SUCCESS ? memory : nullptr;
}

}  // namespace

bool openclFillTiles(Frame& frame, const std::vector<Tile>& tiles, int originX, int originY, int mapW, int mapH,
                     int dividerPx) {
  if (mapW < 64 || mapH < 64 || tiles.size() < 24) return false;
  static std::once_flag once;
  std::call_once(once, []() { initApi(); });
  Api& cl = api();
  if (!cl.ready) return false;

  constexpr int binSize = 64;
  const int binsX = (mapW + binSize - 1) / binSize;
  const int binsY = (mapH + binSize - 1) / binSize;
  std::vector<std::vector<int>> cells(static_cast<size_t>(binsX * binsY));
  std::vector<ClTile> packed;
  packed.reserve(tiles.size());
  for (const Tile& tile : tiles) {
    int x0 = static_cast<int>(std::floor(tile.outer.x));
    int y0 = static_cast<int>(std::floor(tile.outer.y));
    int x1 = static_cast<int>(std::floor(tile.outer.x + tile.outer.w));
    int y1 = static_cast<int>(std::floor(tile.outer.y + tile.outer.h));
    int w = x1 - x0;
    int h = y1 - y0;
    if (w <= 0 || h <= 0) {
      packed.push_back(ClTile{});
      continue;
    }
    uint32_t base = categoryColor(tile.flags & kDir ? CatDir : tile.category);
    if (tile.flags & kFree) base = categoryColor(CatFree);
    if (tile.flags & kPending) base = 0xf5cba7;
    int titleH = static_cast<int>(std::floor(tile.title.y + tile.title.h) - std::floor(tile.title.y));
    if (titleH < 0) titleH = 0;
    if (titleH > h) titleH = h;
    ClTile item{};
    item.x = x0;
    item.y = y0;
    item.w = w;
    item.h = h;
    item.titleH = titleH;
    item.divH = titleH > 3 ? dividerPx : 0;
    item.top = shadeColor(base, 5);
    item.bot = shadeColor(base, -5);
    item.title = shadeColor(base, 12);
    item.divider = shadeColor(base, -30);
    int id = static_cast<int>(packed.size());
    packed.push_back(item);
    int bx0 = std::max(0, (x0 - originX) / binSize);
    int by0 = std::max(0, (y0 - originY) / binSize);
    int bx1 = std::min(binsX - 1, (x1 - 1 - originX) / binSize);
    int by1 = std::min(binsY - 1, (y1 - 1 - originY) / binSize);
    if (x1 <= originX || y1 <= originY || x0 >= originX + mapW || y0 >= originY + mapH) continue;
    for (int by = by0; by <= by1; ++by) {
      for (int bx = bx0; bx <= bx1; ++bx) cells[static_cast<size_t>(by * binsX + bx)].push_back(id);
    }
  }
  std::vector<int> indices;
  std::vector<Bin> bins(static_cast<size_t>(binsX * binsY));
  for (int i = 0; i < binsX * binsY; ++i) {
    bins[static_cast<size_t>(i)].start = static_cast<int>(indices.size());
    bins[static_cast<size_t>(i)].count = static_cast<int>(cells[static_cast<size_t>(i)].size());
    indices.insert(indices.end(), cells[static_cast<size_t>(i)].begin(), cells[static_cast<size_t>(i)].end());
  }
  if (indices.empty()) indices.push_back(0);

  const size_t pixelsNeed = frame.bgra.size() * sizeof(uint32_t);
  const size_t tilesNeed = std::max<size_t>(packed.size(), 1) * sizeof(ClTile);
  const size_t indexNeed = indices.size() * sizeof(int);
  const size_t binNeed = bins.size() * sizeof(Bin);
  cl.pixels = grow(cl.pixels, cl.pixelBytes, pixelsNeed, CL_MEM_READ_WRITE);
  cl.tileBuf = grow(cl.tileBuf, cl.tileBytes, tilesNeed, CL_MEM_READ_ONLY);
  cl.indexBuf = grow(cl.indexBuf, cl.indexBytes, indexNeed, CL_MEM_READ_ONLY);
  cl.binBuf = grow(cl.binBuf, cl.binBytes, binNeed, CL_MEM_READ_ONLY);
  if (!cl.pixels || !cl.tileBuf || !cl.indexBuf || !cl.binBuf) return false;
  if (cl.writeBuffer(cl.queue, cl.pixels, 1, 0, pixelsNeed, frame.bgra.data(), 0, nullptr, nullptr) != CL_SUCCESS)
    return false;
  if (cl.writeBuffer(cl.queue, cl.tileBuf, 1, 0, packed.size() * sizeof(ClTile), packed.data(), 0, nullptr, nullptr) !=
      CL_SUCCESS)
    return false;
  if (cl.writeBuffer(cl.queue, cl.indexBuf, 1, 0, indexNeed, indices.data(), 0, nullptr, nullptr) != CL_SUCCESS)
    return false;
  if (cl.writeBuffer(cl.queue, cl.binBuf, 1, 0, binNeed, bins.data(), 0, nullptr, nullptr) != CL_SUCCESS) return false;

  int width = frame.width;
  int div = dividerPx;
  int arg = 0;
  cl.setArg(cl.kernel, arg++, sizeof(cl_mem), &cl.pixels);
  cl.setArg(cl.kernel, arg++, sizeof(int), &width);
  cl.setArg(cl.kernel, arg++, sizeof(int), &originX);
  cl.setArg(cl.kernel, arg++, sizeof(int), &originY);
  cl.setArg(cl.kernel, arg++, sizeof(int), &mapW);
  cl.setArg(cl.kernel, arg++, sizeof(int), &mapH);
  cl.setArg(cl.kernel, arg++, sizeof(int), &binSize);
  cl.setArg(cl.kernel, arg++, sizeof(int), &binsX);
  cl.setArg(cl.kernel, arg++, sizeof(cl_mem), &cl.tileBuf);
  cl.setArg(cl.kernel, arg++, sizeof(cl_mem), &cl.indexBuf);
  cl.setArg(cl.kernel, arg++, sizeof(cl_mem), &cl.binBuf);
  (void)div;
  size_t global[2] = {static_cast<size_t>((mapW + 15) / 16 * 16), static_cast<size_t>((mapH + 15) / 16 * 16)};
  if (cl.enqueue(cl.queue, cl.kernel, 2, nullptr, global, nullptr, 0, nullptr, nullptr) != CL_SUCCESS) return false;
  if (cl.readBuffer(cl.queue, cl.pixels, 1, 0, pixelsNeed, frame.bgra.data(), 0, nullptr, nullptr) != CL_SUCCESS)
    return false;
  return cl.finish(cl.queue) == CL_SUCCESS;
}
