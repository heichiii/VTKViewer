#ifndef GLWIDGET_HPP
#define GLWIDGET_HPP

#include <QOpenGLWidget>
#include <QOpenGLFunctions_4_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QElapsedTimer>
#include <QTimer>
#include <memory>

#include "Camera.hpp"
#include "MeshProcessor.hpp"
#include "Loader.hpp"

class GLWidget : public QOpenGLWidget, protected QOpenGLFunctions_4_3_Core
{
    Q_OBJECT

public:
    enum RenderMode {
        Solid = 0,
        Wireframe = 1,
        Points = 2,
        SolidWireframe = 3,
        Surface = 4  // 双面渲染模式，用于查看内部
    };

    enum ColorMode {
        SolidColor = 0,
        PointData = 1,
        CellData = 2,
        NormalColor = 3
    };

    explicit GLWidget(QWidget *parent = nullptr);
    ~GLWidget() override;

    bool loadMesh(const QString& filePath);
    void resetCamera();
    
    void setRenderMode(RenderMode mode);
    void setColorMode(ColorMode mode);
    void setActiveDataArray(const QString& name);
    void setPointSize(int size);
    void setLineWidth(int width);
    
    QPair<int64_t, int64_t> getMeshStats() const;
    QStringList getPointDataArrayNames() const;
    QStringList getCellDataArrayNames() const;

signals:
    void statusMessage(const QString& message);
    void meshLoaded();
    void dataArraysUpdated();

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void setupShaders();
    void setupBuffers();
    void updateBuffers();
    void renderMesh();
    void renderAxes();
    
    // Shaders
    std::unique_ptr<QOpenGLShaderProgram> m_meshShader;
    std::unique_ptr<QOpenGLShaderProgram> m_wireShader;
    std::unique_ptr<QOpenGLShaderProgram> m_axesShader;
    
    // Buffers
    QOpenGLVertexArrayObject m_meshVAO;
    QOpenGLBuffer m_vertexBuffer;
    QOpenGLBuffer m_triangleIndexBuffer;
    QOpenGLBuffer m_lineIndexBuffer;
    QOpenGLBuffer m_pointIndexBuffer;
    
    QOpenGLVertexArrayObject m_axesVAO;
    QOpenGLBuffer m_axesBuffer;
    
    // Mesh data
    GPUMeshData m_meshData;
    std::shared_ptr<UnstructuredGrid> m_grid;
    MeshProcessor m_processor;
    
    // Camera
    Camera m_camera;
    
    // Mouse interaction
    QPoint m_lastMousePos;
    bool m_leftMousePressed = false;
    bool m_rightMousePressed = false;
    bool m_middleMousePressed = false;
    
    // Render settings
    RenderMode m_renderMode = Solid;
    ColorMode m_colorMode = SolidColor;
    QString m_activeDataArray;
    float m_pointSize = 5.0f;
    float m_lineWidth = 1.0f;
    
    // Colors
    QVector3D m_solidColor = QVector3D(0.7f, 0.7f, 0.8f);
    QVector3D m_wireColor = QVector3D(0.1f, 0.1f, 0.1f);
    QVector3D m_lightDir = QVector3D(0.3f, 1.0f, 0.5f).normalized();
    
    bool m_meshLoaded = false;
    
    // FPS计数
    QElapsedTimer m_fpsTimer;
    int m_frameCount = 0;
    float m_currentFps = 0.0f;
    
    // Uniform locations cache for performance
    GLint m_mvpLoc = -1;
    GLint m_modelViewLoc = -1;
    GLint m_normalMatrixLoc = -1;
    GLint m_lightDirLoc = -1;
    GLint m_solidColorLoc = -1;
    GLint m_colorModeLoc = -1;
    GLint m_twoSidedLightingLoc = -1;
};

#endif // GLWIDGET_HPP
