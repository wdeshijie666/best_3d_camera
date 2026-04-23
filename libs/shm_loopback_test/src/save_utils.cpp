#include "shm_loopback_test/save_utils.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <ctime>
#endif

namespace camera3d::shm_loopback_test {
namespace {

std::filesystem::path CwdRelative(const char* rel) {
  if (!rel || !*rel) return std::filesystem::path(".");
  return std::filesystem::path(rel);
}

}  // namespace

std::string MakeTimestampFolderName() {
#if defined(_WIN32)
  SYSTEMTIME st{};
  ::GetLocalTime(&st);
  const int zz = static_cast<int>(st.wMilliseconds / 10);  // 0-99
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%04u%02u%02u_%02u%02u%02u%02u", static_cast<unsigned>(st.wYear),
                static_cast<unsigned>(st.wMonth), static_cast<unsigned>(st.wDay),
                static_cast<unsigned>(st.wHour), static_cast<unsigned>(st.wMinute),
                static_cast<unsigned>(st.wSecond), static_cast<unsigned>(zz));
  return std::string(buf);
#else
  const auto now = std::chrono::system_clock::now();
  const std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm_buf{};
  localtime_r(&t, &tm_buf);
  const auto ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
  const int zz = static_cast<int>(ms.count() / 10);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%04d%02d%02d_%02d%02d%02d%02d", tm_buf.tm_year + 1900,
                tm_buf.tm_mon + 1, tm_buf.tm_mday, tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, zz);
  return std::string(buf);
#endif
}

std::filesystem::path ResolveSaveRoot(const char* env_var, const char* fallback_relative) {
  if (env_var && *env_var) {
#if defined(_WIN32)
    char buf[32768];
    const DWORD n = ::GetEnvironmentVariableA(env_var, buf, static_cast<DWORD>(sizeof(buf)));
    if (n > 0 && n < sizeof(buf)) {
      return std::filesystem::path(std::string(buf, buf + n));
    }
#else
    if (const char* p = std::getenv(env_var)) {
      return std::filesystem::path(p);
    }
#endif
  }
  return std::filesystem::absolute(CwdRelative(fallback_relative));
}

bool SaveNextSequentialBinary(const std::filesystem::path& root_parent, const std::string& role_segment,
                              const void* data, std::size_t len, std::filesystem::path& out_folder,
                              int& out_index1_based) {
  out_index1_based = 0;
  out_folder.clear();
  if (!data || len == 0) return false;
  try {
    const std::string ts = MakeTimestampFolderName();
    const std::filesystem::path folder = root_parent / role_segment / ts;
    std::filesystem::create_directories(folder);
    int idx = 1;
    for (;;) {
      std::ostringstream oss;
      oss << std::setw(5) << std::setfill('0') << idx << ".bin";
      const std::filesystem::path fp = folder / oss.str();
      if (!std::filesystem::exists(fp)) {
        std::ofstream out(fp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(static_cast<const char*>(data), static_cast<std::streamsize>(len));
        if (!out) return false;
        out_folder = folder;
        out_index1_based = idx;
        return true;
      }
      ++idx;
      if (idx > 99999) return false;
    }
  } catch (...) {
    return false;
  }
}

}  // namespace camera3d::shm_loopback_test
