#pragma once

// Windows 下 camera_sdk DLL 导出/导入；定义 CAMERA_SDK_BUILD 的翻译单元导出符号。

#if defined(_WIN32)
#if defined(CAMERA_SDK_BUILD)
#define CAMERA_SDK_API __declspec(dllexport)
#else
#define CAMERA_SDK_API __declspec(dllimport)
#endif
#else
#if defined(CAMERA_SDK_BUILD)
#define CAMERA_SDK_API __attribute__((visibility("default")))
#else
#define CAMERA_SDK_API
#endif
#endif
