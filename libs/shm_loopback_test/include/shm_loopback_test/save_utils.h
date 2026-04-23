#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace camera3d::shm_loopback_test {

// 本地时间文件夹名：yyyyMMdd_hhmmsszz（zz 为厘秒 00-99，由毫秒/10 得到）
std::string MakeTimestampFolderName();

// 根目录：环境变量 env_var；未设置则用 fallback_root（相对当前工作目录）
std::filesystem::path ResolveSaveRoot(const char* env_var, const char* fallback_relative);

// 在 root_parent / role_segment / <时间戳>/ 下写入 00001.bin、00002.bin ...
// 返回是否成功；成功时写入 out_folder 与 next_index（本次写入使用的序号）
bool SaveNextSequentialBinary(const std::filesystem::path& root_parent, const std::string& role_segment,
                              const void* data, std::size_t len, std::filesystem::path& out_folder,
                              int& out_index1_based);

}  // namespace camera3d::shm_loopback_test
