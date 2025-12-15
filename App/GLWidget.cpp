#include "GLWidget.hpp"
#include "LoaderFactory.hpp"
#include <QFile>
#include <QTextStream>
#include <QElapsedTimer>
#include <cmath>

GLWidget::GLWidget(QWidget *parent)
    : QOpenGLWidget(parent)
    , m_vertexBuffer(QOpenGLBuffer::VertexBuffer)
    , m_triangleIndexBuffer(QOpenGLBuffer::IndexBuffer)
    , m_lineIndexBuffer(QOpenGLBuffer::IndexBuffer)
    , m_pointIndexBuffer(QOpenGLBuffer::IndexBuffer)
    , m_axesBuffer(QOpenGLBuffer::VertexBuffer)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}

GLWidget::~GLWidget()
{
    makeCurrent();
    
    m_meshVAO.destroy();
    m_vertexBuffer.destroy();
    m_triangleIndexBuffer.destroy();
    m_lineIndexBuffer.destroy();
    m_pointIndexBuffer.destroy();
    
    m_axesVAO.destroy();
    m_axesBuffer.destroy();
    
    doneCurrent();
}


void GLWidget::initializeGL() {
    initializeOpenGLFunctions();

    // 设置清屏颜色和深度
    glClearColor(0.15f, 0.15f, 0.18f, 1.0f);
    glClearDepth(1.0f);

    // 启用深度测试（3D渲染必需）
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);  // 明确指定深度测试函数

    // 启用面剔除（提高性能）
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);  // 逆时针为正面

    // 多重采样（如果可用）
    GLint samples = 0;
    glGetIntegerv(GL_SAMPLES, &samples);
    if (samples > 0) {
        glEnable(GL_MULTISAMPLE);
    }

    // 混合（用于透明效果）
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // 预乘alpha混合（更好）
    // glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    // 允许程序控制点大小
    glEnable(GL_PROGRAM_POINT_SIZE);

    // 多边形偏移（防止深度冲突）
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);

    // 检查OpenGL错误
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        qDebug() << "OpenGL Error during initialization:" << err;
    }

    setupShaders();
    setupBuffers();

    // 输出OpenGL信息
    qDebug() << "OpenGL Version:" << (const char*)glGetString(GL_VERSION);
    qDebug() << "GLSL Version:" << (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION);
    qDebug() << "Renderer:" << (const char*)glGetString(GL_RENDERER);
    qDebug() << "Vendor:" << (const char*)glGetString(GL_VENDOR);
}
// void GLWidget::initializeGL()
// {
//     initializeOpenGLFunctions();
//
//     glClearColor(0.15f, 0.15f, 0.18f, 1.0f);
//     glEnable(GL_DEPTH_TEST);
//     glEnable(GL_MULTISAMPLE);
//     // glEnable(GL_LINE_SMOOTH);
//     glEnable(GL_PROGRAM_POINT_SIZE);
//     // glEnable(GL_POINT_SMOOTH);  // 启用点平滑 弃用
//     glEnable(GL_BLEND);         // 启用混合以支持平滑
//     glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
//     // glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);
//     // glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
//
//     // Polygon offset for wireframe overlay
//     glPolygonOffset(1.0f, 1.0f);
//
//     setupShaders();
//     setupBuffers();
//
//     emit statusMessage("OpenGL " + QString((const char*)glGetString(GL_VERSION)));
// }

void GLWidget::setupShaders()
{
    // Main mesh shader
    m_meshShader = std::make_unique<QOpenGLShaderProgram>();
    
    // Load shaders from resources
    m_meshShader->addShaderFromSourceFile(QOpenGLShader::Vertex, ":/shaders/mesh.vert");
    m_meshShader->addShaderFromSourceFile(QOpenGLShader::Fragment, ":/shaders/mesh.frag");
    m_meshShader->link();
    
    // Cache uniform locations for performance
    m_mvpLoc = m_meshShader->uniformLocation("mvp");
    m_modelViewLoc = m_meshShader->uniformLocation("modelView");
    m_normalMatrixLoc = m_meshShader->uniformLocation("normalMatrix");
    m_lightDirLoc = m_meshShader->uniformLocation("lightDir");
    m_solidColorLoc = m_meshShader->uniformLocation("solidColor");
    m_colorModeLoc = m_meshShader->uniformLocation("colorMode");
    m_twoSidedLightingLoc = m_meshShader->uniformLocation("twoSidedLighting");
    
    // Wireframe shader
    m_wireShader = std::make_unique<QOpenGLShaderProgram>();
    m_wireShader->addShaderFromSourceFile(QOpenGLShader::Vertex, ":/shaders/wire.vert");
    m_wireShader->addShaderFromSourceFile(QOpenGLShader::Fragment, ":/shaders/wire.frag");
    m_wireShader->link();
    
    // Axes shader
    m_axesShader = std::make_unique<QOpenGLShaderProgram>();
    m_axesShader->addShaderFromSourceFile(QOpenGLShader::Vertex, ":/shaders/axes.vert");
    m_axesShader->addShaderFromSourceFile(QOpenGLShader::Fragment, ":/shaders/axes.frag");
    m_axesShader->link();
    
    // 启动FPS计时器
    m_fpsTimer.start();
}

void GLWidget::setupBuffers()
{
    // Mesh VAO
    m_meshVAO.create();
    m_meshVAO.bind();
    
    m_vertexBuffer.create();
    m_vertexBuffer.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    
    m_triangleIndexBuffer.create();
    m_triangleIndexBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
    
    m_lineIndexBuffer.create();
    m_lineIndexBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
    
    m_pointIndexBuffer.create();
    m_pointIndexBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
    
    m_meshVAO.release();
    
    // Axes VAO
    m_axesVAO.create();
    m_axesVAO.bind();
    
    // Axes vertices: position (3) + color (3)
    static const float axesVertices[] = {
        // X axis - red
        0.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,
        // Y axis - green
        0.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,
        0.0f, 1.0f, 0.0f,  0.0f, 1.0f, 0.0f,
        // Z axis - blue
        0.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f,
    };
    
    m_axesBuffer.create();
    m_axesBuffer.bind();
    m_axesBuffer.allocate(axesVertices, sizeof(axesVertices));
    
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), 
                          reinterpret_cast<void*>(3 * sizeof(float)));
    
    m_axesVAO.release();
}

void GLWidget::updateBuffers()
{
    if (m_meshData.vertexData.empty()) return;
    
    m_meshVAO.bind();
    
    // Upload vertex data
    m_vertexBuffer.bind();
    m_vertexBuffer.allocate(m_meshData.vertexData.data(), 
                            static_cast<int>(m_meshData.vertexData.size() * sizeof(float)));
    
    // Setup vertex attributes
    // Position (3 floats)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), nullptr);
    
    // Normal (3 floats)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), 
                          reinterpret_cast<void*>(3 * sizeof(float)));
    
    // Scalar (1 float)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 7 * sizeof(float), 
                          reinterpret_cast<void*>(6 * sizeof(float)));
    
    m_vertexBuffer.release();
    
    // Upload index buffers
    if (!m_meshData.triangleIndices.empty()) {
        m_triangleIndexBuffer.bind();
        m_triangleIndexBuffer.allocate(m_meshData.triangleIndices.data(),
                                       static_cast<int>(m_meshData.triangleIndices.size() * sizeof(uint32_t)));
        m_triangleIndexBuffer.release();
    }
    
    if (!m_meshData.lineIndices.empty()) {
        m_lineIndexBuffer.bind();
        m_lineIndexBuffer.allocate(m_meshData.lineIndices.data(),
                                   static_cast<int>(m_meshData.lineIndices.size() * sizeof(uint32_t)));
        m_lineIndexBuffer.release();
    }
    
    if (!m_meshData.pointIndices.empty()) {
        m_pointIndexBuffer.bind();
        m_pointIndexBuffer.allocate(m_meshData.pointIndices.data(),
                                    static_cast<int>(m_meshData.pointIndices.size() * sizeof(uint32_t)));
        m_pointIndexBuffer.release();
    }
    
    m_meshVAO.release();
}

void GLWidget::resizeGL(int w, int h)
{
    m_camera.setAspectRatio(static_cast<float>(w) / static_cast<float>(h));
    glViewport(0, 0, w, h);
}

void GLWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    if (m_meshLoaded) {
        renderMesh();
    }
    
    renderAxes();
    
    // FPS计数
    m_frameCount++;
    qint64 elapsed = m_fpsTimer.elapsed();
    if (elapsed >= 1000) {
        m_currentFps = m_frameCount * 1000.0f / elapsed;
        emit statusMessage(QString("FPS: %1 | Triangles: %2")
                          .arg(m_currentFps, 0, 'f', 1)
                          .arg(m_meshData.triangleCount));
        m_frameCount = 0;
        m_fpsTimer.restart();
    }
}

void GLWidget::renderMesh()
{
    QMatrix4x4 mvp = m_camera.projectionMatrix() * m_camera.viewMatrix();
    QMatrix4x4 modelView = m_camera.viewMatrix();
    QMatrix3x3 normalMatrix = modelView.normalMatrix();
    
    m_meshVAO.bind();
    
    // Render based on mode
    switch (m_renderMode) {
        case Solid:
            glDisable(GL_CULL_FACE);  // 禁用背面剔除，确保所有面都渲染
            m_meshShader->bind();
            m_meshShader->setUniformValue("mvp", mvp);
            m_meshShader->setUniformValue("modelView", modelView);
            m_meshShader->setUniformValue("normalMatrix", normalMatrix);
            m_meshShader->setUniformValue("lightDir", m_lightDir);
            m_meshShader->setUniformValue("solidColor", m_solidColor);
            m_meshShader->setUniformValue("colorMode", static_cast<int>(m_colorMode));
            m_meshShader->setUniformValue("scalarMin", m_meshData.scalarMin);
            m_meshShader->setUniformValue("scalarMax", m_meshData.scalarMax);
            m_meshShader->setUniformValue("twoSidedLighting", 1);  // 启用双面光照
            m_meshShader->setUniformValue("renderPoints", 0);  // 非点渲染
            
            m_triangleIndexBuffer.bind();
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_meshData.triangleIndices.size()), 
                          GL_UNSIGNED_INT, nullptr);
            m_triangleIndexBuffer.release();
            m_meshShader->release();
            break;
            
        case Wireframe:
            glDisable(GL_CULL_FACE);
            m_wireShader->bind();
            m_wireShader->setUniformValue("mvp", mvp);
            m_wireShader->setUniformValue("color", m_wireColor);
            glLineWidth(m_lineWidth);
            
            m_lineIndexBuffer.bind();
            glDrawElements(GL_LINES, static_cast<GLsizei>(m_meshData.lineIndices.size()), 
                          GL_UNSIGNED_INT, nullptr);
            m_lineIndexBuffer.release();
            m_wireShader->release();
            break;
            
        case Points:
            glDisable(GL_CULL_FACE);
            m_meshShader->bind();
            m_meshShader->setUniformValue("mvp", mvp);
            m_meshShader->setUniformValue("modelView", modelView);
            m_meshShader->setUniformValue("normalMatrix", normalMatrix);
            m_meshShader->setUniformValue("lightDir", m_lightDir);
            m_meshShader->setUniformValue("solidColor", m_solidColor);
            m_meshShader->setUniformValue("colorMode", static_cast<int>(m_colorMode));
            m_meshShader->setUniformValue("scalarMin", m_meshData.scalarMin);
            m_meshShader->setUniformValue("scalarMax", m_meshData.scalarMax);
            m_meshShader->setUniformValue("pointSize", m_pointSize);
            m_meshShader->setUniformValue("twoSidedLighting", 0);
            m_meshShader->setUniformValue("renderPoints", 1);  // 启用点渲染（圆形点）
            
            m_pointIndexBuffer.bind();
            glDrawElements(GL_POINTS, static_cast<GLsizei>(m_meshData.pointIndices.size()), 
                          GL_UNSIGNED_INT, nullptr);
            m_pointIndexBuffer.release();
            m_meshShader->release();
            break;
            
        case SolidWireframe:
            // First pass: solid
            glDisable(GL_CULL_FACE);  // 禁用背面剔除
            glEnable(GL_POLYGON_OFFSET_FILL);
            m_meshShader->bind();
            m_meshShader->setUniformValue("mvp", mvp);
            m_meshShader->setUniformValue("modelView", modelView);
            m_meshShader->setUniformValue("normalMatrix", normalMatrix);
            m_meshShader->setUniformValue("lightDir", m_lightDir);
            m_meshShader->setUniformValue("solidColor", m_solidColor);
            m_meshShader->setUniformValue("colorMode", static_cast<int>(m_colorMode));
            m_meshShader->setUniformValue("scalarMin", m_meshData.scalarMin);
            m_meshShader->setUniformValue("scalarMax", m_meshData.scalarMax);
            m_meshShader->setUniformValue("twoSidedLighting", 1);  // 启用双面光照
            m_meshShader->setUniformValue("renderPoints", 0);  // 非点渲染
            
            m_triangleIndexBuffer.bind();
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_meshData.triangleIndices.size()), 
                          GL_UNSIGNED_INT, nullptr);
            m_triangleIndexBuffer.release();
            m_meshShader->release();
            glDisable(GL_POLYGON_OFFSET_FILL);
            
            // Second pass: wireframe
            glDisable(GL_CULL_FACE);
            m_wireShader->bind();
            m_wireShader->setUniformValue("mvp", mvp);
            m_wireShader->setUniformValue("color", m_wireColor);
            glLineWidth(m_lineWidth);
            
            m_lineIndexBuffer.bind();
            glDrawElements(GL_LINES, static_cast<GLsizei>(m_meshData.lineIndices.size()), 
                          GL_UNSIGNED_INT, nullptr);
            m_lineIndexBuffer.release();
            m_wireShader->release();
            break;
            
        case Surface:
            // Surface mode: 双面渲染，禁用背面剔除，双面光照
            glDisable(GL_CULL_FACE);  // 禁用背面剔除，可以看到内部
            m_meshShader->bind();
            m_meshShader->setUniformValue("mvp", mvp);
            m_meshShader->setUniformValue("modelView", modelView);
            m_meshShader->setUniformValue("normalMatrix", normalMatrix);
            m_meshShader->setUniformValue("lightDir", m_lightDir);
            m_meshShader->setUniformValue("solidColor", m_solidColor);
            m_meshShader->setUniformValue("colorMode", static_cast<int>(m_colorMode));
            m_meshShader->setUniformValue("scalarMin", m_meshData.scalarMin);
            m_meshShader->setUniformValue("scalarMax", m_meshData.scalarMax);
            m_meshShader->setUniformValue("twoSidedLighting", 1);  // 启用双面光照
            m_meshShader->setUniformValue("renderPoints", 0);  // 非点渲染
            
            m_triangleIndexBuffer.bind();
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_meshData.triangleIndices.size()), 
                          GL_UNSIGNED_INT, nullptr);
            m_triangleIndexBuffer.release();
            m_meshShader->release();
            break;
    }
    
    m_meshVAO.release();
}

void GLWidget::renderAxes()
{
    // Render small axes indicator in corner
    float axisLength = 0.1f;
    
    QMatrix4x4 axisView;
    axisView.setToIdentity();
    // Use only rotation from camera view
    QMatrix4x4 view = m_camera.viewMatrix();
    QMatrix3x3 rotation = view.normalMatrix();
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            axisView(i, j) = rotation(i, j);
    axisView.translate(0, 0, -3);
    
    QMatrix4x4 axisProj;
    axisProj.setToIdentity();
    axisProj.perspective(45.0f, 1.0f, 0.1f, 100.0f);
    
    // Set viewport to corner
    int cornerSize = std::min(width(), height()) / 6;
    glViewport(10, 10, cornerSize, cornerSize);
    glDisable(GL_DEPTH_TEST);
    
    m_axesVAO.bind();
    m_axesShader->bind();
    m_axesShader->setUniformValue("mvp", axisProj * axisView);
    
    glLineWidth(2.0f);
    glDrawArrays(GL_LINES, 0, 6);
    
    m_axesShader->release();
    m_axesVAO.release();
    
    // Restore viewport
    glEnable(GL_DEPTH_TEST);
    glViewport(0, 0, width(), height());
}

bool GLWidget::loadMesh(const QString& filePath)
{
    QElapsedTimer timer;
    timer.start();
    
    auto loader = LoaderFactory::createLoader(filePath.toStdString());
    if (!loader) {
        emit statusMessage("Failed to create loader for: " + filePath);
        return false;
    }
    
    if (!loader->load()) {
        emit statusMessage("Failed to load file: " + filePath);
        return false;
    }
    
    qint64 loadTime = timer.elapsed();
    timer.restart();
    
    m_grid = loader->getGrid();
    if (!m_grid) {
        emit statusMessage("Failed to get grid data");
        return false;
    }
    
    // Process mesh data
    m_meshData = m_processor.process(m_grid);
    
    qint64 processTime = timer.elapsed();
    timer.restart();
    
    // Update OpenGL buffers
    makeCurrent();
    updateBuffers();
    doneCurrent();
    
    qint64 uploadTime = timer.elapsed();
    
    // Fit camera to model
    m_camera.fitToBox(m_meshData.boundingBoxMin, m_meshData.boundingBoxMax);
    
    m_meshLoaded = true;
    
    emit statusMessage(QString("Load: %1ms, Process: %2ms, Upload: %3ms")
                       .arg(loadTime).arg(processTime).arg(uploadTime));
    emit meshLoaded();
    emit dataArraysUpdated();
    
    update();
    return true;
}

void GLWidget::resetCamera()
{
    if (m_meshLoaded) {
        m_camera.fitToBox(m_meshData.boundingBoxMin, m_meshData.boundingBoxMax);
    } else {
        m_camera.reset();
    }
    update();
}

void GLWidget::setRenderMode(RenderMode mode)
{
    m_renderMode = mode;
    update();
}

void GLWidget::setColorMode(ColorMode mode)
{
    m_colorMode = mode;
    update();
}

void GLWidget::setActiveDataArray(const QString& name)
{
    m_activeDataArray = name;
    
    if (!m_grid || name.isEmpty()) return;
    
    bool isPointData = (m_colorMode == PointData);
    m_processor.updateScalars(m_meshData, m_grid, name.toStdString(), isPointData);
    
    // Update vertex buffer with new scalar values
    makeCurrent();
    m_vertexBuffer.bind();
    m_vertexBuffer.write(0, m_meshData.vertexData.data(),
                         static_cast<int>(m_meshData.vertexData.size() * sizeof(float)));
    m_vertexBuffer.release();
    doneCurrent();
    
    update();
}

void GLWidget::setPointSize(int size)
{
    m_pointSize = static_cast<float>(size);
    update();
}

void GLWidget::setLineWidth(int width)
{
    m_lineWidth = static_cast<float>(width);
    update();
}

QPair<int64_t, int64_t> GLWidget::getMeshStats() const
{
    if (m_grid) {
        return {m_grid->num_points, m_grid->num_cells};
    }
    return {0, 0};
}

QStringList GLWidget::getPointDataArrayNames() const
{
    return m_processor.getPointDataArrayNames();
}

QStringList GLWidget::getCellDataArrayNames() const
{
    return m_processor.getCellDataArrayNames();
}

void GLWidget::mousePressEvent(QMouseEvent *event)
{
    m_lastMousePos = event->pos();
    
    if (event->button() == Qt::LeftButton) {
        m_leftMousePressed = true;
    } else if (event->button() == Qt::RightButton) {
        m_rightMousePressed = true;
    } else if (event->button() == Qt::MiddleButton) {
        m_middleMousePressed = true;
    }
}

void GLWidget::mouseMoveEvent(QMouseEvent *event)
{
    QPoint delta = event->pos() - m_lastMousePos;
    
    if (m_leftMousePressed) {
        m_camera.rotate(delta.x(), delta.y());
        update();
    } else if (m_rightMousePressed || m_middleMousePressed) {
        m_camera.pan(delta.x(), delta.y());
        update();
    }
    
    m_lastMousePos = event->pos();
}

void GLWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_leftMousePressed = false;
    } else if (event->button() == Qt::RightButton) {
        m_rightMousePressed = false;
    } else if (event->button() == Qt::MiddleButton) {
        m_middleMousePressed = false;
    }
}

void GLWidget::wheelEvent(QWheelEvent *event)
{
    m_camera.zoom(event->angleDelta().y());
    update();
}
