#include "MainWindow.hpp"
#include "GLWidget.hpp"
#include <QApplication>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("Simple VTK Viewer");
    setupUI();
    setupToolBar();
    setupDockWidget();
    setupConnections();
    
    statusBar()->showMessage("Ready. Open a VTK file to begin.");
}

void MainWindow::setupUI()
{
    m_glWidget = new GLWidget(this);
    setCentralWidget(m_glWidget);
    
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setVisible(false);
    m_progressBar->setMaximumWidth(200);
    
    m_statsLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_progressBar);
    statusBar()->addPermanentWidget(m_statsLabel);
}

void MainWindow::setupToolBar()
{
    m_toolBar = addToolBar("Main Toolbar");
    m_toolBar->setMovable(false);
    
    m_openAction = m_toolBar->addAction("📂 打开文件");
    m_openAction->setShortcut(QKeySequence::Open);
    m_openAction->setToolTip("Open VTK file (Ctrl+O)");
    
    m_toolBar->addSeparator();
    
    m_resetCameraAction = m_toolBar->addAction("🎯 重置视角");
    m_resetCameraAction->setShortcut(QKeySequence(Qt::Key_R));
    m_resetCameraAction->setToolTip("Reset camera to fit model (R)");
}

void MainWindow::setupDockWidget()
{
    m_controlDock = new QDockWidget("Controls", this);
    m_controlDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    
    QWidget* controlWidget = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(controlWidget);
    layout->setSpacing(10);
    
    // Render Mode Group
    QGroupBox* renderGroup = new QGroupBox("渲染模式");
    QVBoxLayout* renderLayout = new QVBoxLayout(renderGroup);
    
    m_renderModeCombo = new QComboBox();
    m_renderModeCombo->addItem("实体", 0);//solid
    m_renderModeCombo->addItem("线框", 1);//Wireframe
    m_renderModeCombo->addItem("点云", 2);//Points
    m_renderModeCombo->addItem("实体+线框", 3);//Solid Wireframe
    m_renderModeCombo->addItem("表面", 4);  // Surface双面表面渲染
    renderLayout->addWidget(m_renderModeCombo);
    
    QLabel* pointSizeLabel = new QLabel("点大小:");
    m_pointSizeSlider = new QSlider(Qt::Horizontal);
    m_pointSizeSlider->setRange(1, 20);
    m_pointSizeSlider->setValue(5);
    renderLayout->addWidget(pointSizeLabel);
    renderLayout->addWidget(m_pointSizeSlider);
    
    QLabel* lineWidthLabel = new QLabel("线宽:");
    m_lineWidthSlider = new QSlider(Qt::Horizontal);
    m_lineWidthSlider->setRange(1, 10);
    m_lineWidthSlider->setValue(1);
    renderLayout->addWidget(lineWidthLabel);
    renderLayout->addWidget(m_lineWidthSlider);
    
    layout->addWidget(renderGroup);
    
    // Physical Value Group
    QGroupBox* physicalGroup = new QGroupBox("物理量选择");
    QVBoxLayout* physicalLayout = new QVBoxLayout(physicalGroup);
    
    m_physicalValueCombo = new QComboBox();
    m_physicalValueCombo->addItem("模型", 0);
    m_physicalValueCombo->addItem("点数据", 1);
    m_physicalValueCombo->addItem("单元数据", 2);
    m_physicalValueCombo->addItem("法向", 3);
    physicalLayout->addWidget(m_physicalValueCombo);
    
    QLabel* dataArrayLabel = new QLabel("物理量:");
    m_dataArrayCombo = new QComboBox();
    m_dataArrayCombo->setEnabled(false);
    physicalLayout->addWidget(dataArrayLabel);
    physicalLayout->addWidget(m_dataArrayCombo);
    
    layout->addWidget(physicalGroup);
    //Color Mode Group
    QGroupBox* colorGroup = new QGroupBox("配色方案");
    QVBoxLayout* colorLayout = new QVBoxLayout(colorGroup);

    m_colorModeCombo=new QComboBox();
    m_colorModeCombo->addItem("viridis",0);
    m_colorModeCombo->addItem("jet",1);
    m_colorModeCombo->addItem("rainbow",2);
    colorLayout->addWidget(m_colorModeCombo);
    layout->addWidget(colorGroup);


    layout->addStretch();
    
    m_controlDock->setWidget(controlWidget);
    addDockWidget(Qt::RightDockWidgetArea, m_controlDock);
}

void MainWindow::setupConnections()
{
    connect(m_openAction, &QAction::triggered, this, &MainWindow::openFile);
    connect(m_resetCameraAction, &QAction::triggered, this, &MainWindow::resetCamera);
    
    connect(m_renderModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onRenderModeChanged);
    connect(m_physicalValueCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onPhysicalValueChanged);
    connect(m_colorModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onColorModeChanged);
    connect(m_dataArrayCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onDataArrayChanged);
    
    connect(m_pointSizeSlider, &QSlider::valueChanged, m_glWidget, &GLWidget::setPointSize);
    connect(m_lineWidthSlider, &QSlider::valueChanged, m_glWidget, &GLWidget::setLineWidth);
    
    connect(m_glWidget, &GLWidget::statusMessage, this, &MainWindow::updateStatusBar);
    connect(m_glWidget, &GLWidget::meshLoaded, this, &MainWindow::onLoadingFinished);
    connect(m_glWidget, &GLWidget::dataArraysUpdated, this, &MainWindow::updateDataArrayList);
}

void MainWindow::openFile()
{
    QString fileName = QFileDialog::getOpenFileName(this,
        "Open VTK File",
        QString(),
        "VTK Files (*.vtk);;All Files (*)");
    
    if (fileName.isEmpty())
        return;
    
    m_progressBar->setVisible(true);
    m_progressBar->setValue(0);
    statusBar()->showMessage("Loading: " + fileName);
    
    QApplication::processEvents();
    
    QElapsedTimer timer;
    timer.start();
    
    bool success = m_glWidget->loadMesh(fileName);
    
    qint64 elapsed = timer.elapsed();
    
    m_progressBar->setVisible(false);
    
    if (success) {
        statusBar()->showMessage(QString("Loaded in %1 ms").arg(elapsed));
    } else {
        QMessageBox::critical(this, "Error", "Failed to load VTK file.");
        statusBar()->showMessage("Failed to load file.");
    }
}

void MainWindow::onRenderModeChanged(int index)
{
    m_glWidget->setRenderMode(static_cast<GLWidget::RenderMode>(index));
}

void MainWindow::onPhysicalValueChanged(int index)
{
    m_dataArrayCombo->setEnabled(index == 1 || index == 2);
    m_glWidget->setPhysicalValue(static_cast<GLWidget::PhysicalData>(index));
    updateDataArrayList();
    

    if (m_dataArrayCombo->count() > 0 && (index == 1 || index == 2)) {
        m_glWidget->setActiveDataArray(m_dataArrayCombo->currentText());
    }
}

void MainWindow::onDataArrayChanged(int index)
{
    if (index >= 0) {
        m_glWidget->setActiveDataArray(m_dataArrayCombo->currentText());
    }
}

void MainWindow::onColorModeChanged(int index)
{
    m_glWidget->setColorMode(static_cast<GLWidget::ColorMode>(index));
}

void MainWindow::resetCamera()
{
    m_glWidget->resetCamera();
}

void MainWindow::updateStatusBar(const QString& message)
{
    statusBar()->showMessage(message);
}

void MainWindow::onLoadingProgress(int progress)
{
    m_progressBar->setValue(progress);
}

void MainWindow::onLoadingFinished()
{
    auto stats = m_glWidget->getMeshStats();
    m_statsLabel->setText(QString("Points: %1 | Cells: %2")
        .arg(stats.first).arg(stats.second));
    updateDataArrayList();
}

void MainWindow::updateDataArrayList()
{
    m_dataArrayCombo->clear();
    
    QStringList arrays;
    if (m_physicalValueCombo->currentIndex() == 1) {
        arrays = m_glWidget->getPointDataArrayNames();
    } else if (m_physicalValueCombo->currentIndex() == 2) {
        arrays = m_glWidget->getCellDataArrayNames();
    }
    
    m_dataArrayCombo->addItems(arrays);
}
