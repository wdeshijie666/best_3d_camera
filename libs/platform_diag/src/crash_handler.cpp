#include "platform_diag/crash_handler.h"

#include "platform_diag/logging.h"

#include <filesystem>
#include <csignal>
#include <iomanip>
#include <sstream>
#include <cstdlib>
#include <exception>
#include <mutex>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <DbgHelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

namespace camera3d::diag {
namespace {

std::mutex g_crash_mu;
std::string g_dump_dir;
std::string g_process_name;

#ifdef _WIN32
LONG WINAPI UnhandledExceptionFilterImpl(EXCEPTION_POINTERS* info) {
  std::scoped_lock lock(g_crash_mu);
  {
    std::ostringstream oss;
    oss << "未处理 SEH 异常，准备写入 MiniDump，code=0x" << std::hex
        << (info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0u);
    if (auto lg = camera3d::diag::DefaultLogger()) {
      lg->error(oss.str());
    }
  }

  std::filesystem::path dir = g_dump_dir;
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);

  const auto stamp =
      std::chrono::system_clock::now().time_since_epoch().count();
  const auto dump_path = (dir / (g_process_name + "_crash_" + std::to_string(stamp) + ".dmp")).wstring();

  HANDLE file = CreateFileW(dump_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    CAMERA3D_LOGE("创建 dump 文件失败");
    return EXCEPTION_EXECUTE_HANDLER;
  }

  MINIDUMP_EXCEPTION_INFORMATION mei{};
  mei.ThreadId = GetCurrentThreadId();
  mei.ExceptionPointers = info;
  mei.ClientPointers = FALSE;

  const MINIDUMP_TYPE dump_type = static_cast<MINIDUMP_TYPE>(
      MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);

  const BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, dump_type, &mei,
                                    nullptr, nullptr);
  CloseHandle(file);

  if (!ok) {
    CAMERA3D_LOGE("MiniDumpWriteDump 失败，GetLastError={}", GetLastError());
  } else {
    CAMERA3D_LOGI("已写入 MiniDump: {}", std::filesystem::path(dump_path).string());
  }

  return EXCEPTION_EXECUTE_HANDLER;
}
#endif

void OnTerminate() {
  try {
    std::exception_ptr e = std::current_exception();
    if (e) {
      std::rethrow_exception(e);
    }
  } catch (const std::exception& ex) {
    CAMERA3D_LOGE("std::terminate：{}", ex.what());
  } catch (...) {
    CAMERA3D_LOGE("std::terminate：未知异常");
  }
  std::abort();
}

}  // namespace

// 实现 InstallCrashHandlers：注册 Windows SEH 与 std::terminate 处理。
void InstallCrashHandlers(const std::string& dump_dir_utf8, std::string_view process_name) {
  std::scoped_lock lock(g_crash_mu);
  g_dump_dir = dump_dir_utf8;
  g_process_name = process_name;

#ifdef _WIN32
  SetUnhandledExceptionFilter(UnhandledExceptionFilterImpl);
#endif

  std::set_terminate(OnTerminate);
}

}  // namespace camera3d::diag
