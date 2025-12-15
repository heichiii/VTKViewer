#include <QApplication>
#include <QSurfaceFormat>
#include "MainWindow.hpp"
#ifdef _WIN32
#include <windows.h>
#include <dxgi.h>
#pragma comment(lib, "dxgi.lib")

// 强制NVIDIA/AMD使用独立显卡
extern "C" {
    __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

bool isUsingDedicatedGPU()
{
    IDXGIFactory* pFactory = nullptr;
    IDXGIAdapter* pAdapter = nullptr;
    std::vector<IDXGIAdapter*> adapters;

    if (SUCCEEDED(CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&pFactory))) {
        for (UINT i = 0;
             pFactory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND;
             ++i) {
            adapters.push_back(pAdapter);
             }

        if (pFactory) pFactory->Release();
    }

    // 通常独立显卡是第二个适配器（索引1）
    // 但需要检查描述来确定
    return adapters.size() > 1;
}
#endif
int main(int argc, char *argv[])
{
#ifdef _WIN32
    // 设置环境变量
    SetEnvironmentVariable(LPCWSTR("SHIM_MCCOMPAT"), LPCWSTR("0x800000001"));

    if (isUsingDedicatedGPU()) {
        qDebug() << "System has dedicated GPU available";
    }
#endif
    // 完整OpenGL
    QApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    
    QApplication app(argc, argv);
    

    QSurfaceFormat format;//表面：屏幕
    format.setVersion(4, 5);//计算着色器
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);//深度精度
    format.setStencilBufferSize(8);//模板缓冲
    format.setSamples(4); // MSAA 4x
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    format.setSwapInterval(0); // 禁用VSync以获得最高帧率
    QSurfaceFormat::setDefaultFormat(format);
    
    MainWindow window;
    window.resize(1280, 720);
    window.show();
    
    return app.exec();
}
