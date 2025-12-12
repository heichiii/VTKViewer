#include <QApplication>
#include <QSurfaceFormat>
#include "MainWindow.hpp"

int main(int argc, char *argv[])
{
    // 启用高DPI支持
    QApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    
    QApplication app(argc, argv);
    
    // Set OpenGL 4.3 Core Profile for compute shaders support
    QSurfaceFormat format;
    format.setVersion(4, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSamples(4); // MSAA 4x
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    format.setSwapInterval(0); // 禁用VSync以获得最高帧率
    QSurfaceFormat::setDefaultFormat(format);
    
    MainWindow window;
    window.resize(1280, 720);
    window.show();
    
    return app.exec();
}
