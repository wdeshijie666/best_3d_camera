/**
 * @file main.cpp
 * @brief 3D 相机客户端入口，Ela 风格初始化，VTK OpenGL 格式
 */
#include "app/MainWindow.h"
#include "common/log/Logger.h"
#include <QApplication>
#include <QSurfaceFormat>
#include <QVTKOpenGLNativeWidget.h>
#include <ElaApplication.h>

int main(int argc, char* argv[]) {
    using namespace scanner_viewer::log;

    // 日志模块初始化（空字符串表示仅控制台，可传文件路径同时写文件）
    Init(/* log_file */ "");
    // 通过级别控制输出：设为 Info 时仅 Info/Warn/Error 输出，Trace/Debug 不输出
    SetLevel(Level::Info);

    LOG_INFO("camera_viewer starting, log level=%d", static_cast<int>(GetLevel()));

    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());
    QApplication app(argc, argv);
    eApp->init();

    scanner_viewer::MainWindow w;
    w.show();
    return app.exec();
}
