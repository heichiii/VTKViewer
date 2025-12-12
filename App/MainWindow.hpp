#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP

#include <QMainWindow>
#include <QToolBar>
#include <QStatusBar>
#include <QDockWidget>
#include <QListWidget>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QProgressBar>
#include <QElapsedTimer>

class GLWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

private slots:
    void openFile();
    void onRenderModeChanged(int index);
    void onColorModeChanged(int index);
    void onDataArrayChanged(int index);
    void resetCamera();
    void updateStatusBar(const QString& message);
    void onLoadingProgress(int progress);
    void onLoadingFinished();

private:
    void setupUI();
    void setupToolBar();
    void setupDockWidget();
    void setupConnections();
    void updateDataArrayList();

    GLWidget* m_glWidget;
    
    // Toolbar
    QToolBar* m_toolBar;
    QAction* m_openAction;
    QAction* m_resetCameraAction;
    
    // Dock widget for controls
    QDockWidget* m_controlDock;
    QComboBox* m_renderModeCombo;
    QComboBox* m_colorModeCombo;
    QComboBox* m_dataArrayCombo;
    QSlider* m_pointSizeSlider;
    QSlider* m_lineWidthSlider;
    
    // Status
    QProgressBar* m_progressBar;
    QLabel* m_statsLabel;
};

#endif // MAINWINDOW_HPP
