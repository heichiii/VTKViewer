#ifndef CAMERA_HPP
#define CAMERA_HPP

#include <QVector3D>
#include <QMatrix4x4>
#include <QtMath>

class Camera
{
public:
    Camera()
        : m_position(0.0f, 0.0f, 5.0f)
        , m_target(0.0f, 0.0f, 0.0f)
        , m_up(0.0f, 1.0f, 0.0f)
        , m_fov(45.0f)
        , m_aspectRatio(1.0f)
        , m_nearPlane(0.01f)
        , m_farPlane(1000.0f)
        , m_distance(5.0f)
        , m_rotationX(0.0f)
        , m_rotationY(0.0f)
    {
        updateViewMatrix();
    }

    void setAspectRatio(float ratio) {
        m_aspectRatio = ratio;
        updateProjectionMatrix();
    }

    void rotate(float dx, float dy) {
        m_rotationY -= dx * 0.5f;  // 反转水平旋转
        m_rotationX += dy * 0.5f;  // 反转垂直旋转
        m_rotationX = qBound(-89.0f, m_rotationX, 89.0f);
        updateViewMatrix();
    }

    // void zoom(float delta) {
    //     m_distance *= (1.0f - delta * 0.001f);
    //     m_distance = qBound(-0.0001f, m_distance, 1000.0f);
    //     updateViewMatrix();
    // }

    void zoom(float delta) {
        float factor = 1.0f - delta * 0.001f;
        factor = qMax(0.001f, factor);
        m_distance *= factor;

        // 基于场景半径设置最小距离，避免逐步进入并被近裁剪面或剔除
        const float absoluteMin = 0.001f;            // 绝对下限，保持深度精度
        const float relativeMin = m_sceneRadius * 0.05f; // 相对于场景的下限，可调整
        const float minDistance = qMax(absoluteMin, relativeMin);
        const float maxDistance = 1000.0f;
        m_distance = qBound(minDistance, m_distance, maxDistance);

        // 保持 nearPlane 有合理下限，避免极小 near 导致深度精度问题
        m_nearPlane = qMax(0.1f, m_distance * 0.01f);
        m_farPlane = qMax(m_nearPlane + 0.1f, m_distance * 100.0f);
        updateProjectionMatrix();
        updateViewMatrix();
    }


    void pan(float dx, float dy) {
        QVector3D right = QVector3D::crossProduct(m_target - m_position, m_up).normalized();
        QVector3D up = QVector3D::crossProduct(right, m_target - m_position).normalized();
        float panSpeed = m_distance * 0.001f;
        m_target -= right * dx * panSpeed;
        m_target += up * dy * panSpeed;
        updateViewMatrix();
    }
    void setSceneRadius(float radius) {
        m_sceneRadius = qMax(0.001f, radius);
    }
    // 对 fitToBox 的小调整：记录场景半径并设置投影裁剪
    void fitToBox(const QVector3D& min, const QVector3D& max) {
        m_target = (min + max) * 0.5f;
        float radius = (max - min).length() * 0.5f;
        m_sceneRadius = qMax(0.001f, radius); // 记录场景半径
        m_distance = radius / qTan(qDegreesToRadians(m_fov * 0.5f)) * 1.5f;
        m_rotationX = 20.0f;
        m_rotationY = -30.0f;
        m_nearPlane = qMax(0.01f, m_distance * 0.01f);
        m_farPlane = m_distance * 100.0f;
        updateViewMatrix();
        updateProjectionMatrix();
    }
    // void fitToBox(const QVector3D& min, const QVector3D& max) {
    //     m_target = (min + max) * 0.5f;
    //     float radius = (max - min).length() * 0.5f;
    //     m_distance = radius / qTan(qDegreesToRadians(m_fov * 0.5f)) * 1.5f;
    //     m_rotationX = 20.0f;
    //     m_rotationY = -30.0f;
    //     m_nearPlane = m_distance * 0.01f;
    //     m_farPlane = m_distance * 100.0f;
    //     updateViewMatrix();
    //     updateProjectionMatrix();
    // }

    void reset() {
        m_rotationX = 20.0f;
        m_rotationY = -30.0f;
        updateViewMatrix();
    }

    QMatrix4x4 viewMatrix() const { return m_viewMatrix; }
    QMatrix4x4 projectionMatrix() const { return m_projectionMatrix; }
    QVector3D position() const { return m_position; }

private:
    void updateViewMatrix() {
        float radX = qDegreesToRadians(m_rotationX);
        float radY = qDegreesToRadians(m_rotationY);
        
        m_position.setX(m_target.x() + m_distance * qCos(radX) * qSin(radY));
        m_position.setY(m_target.y() + m_distance * qSin(radX));
        m_position.setZ(m_target.z() + m_distance * qCos(radX) * qCos(radY));
        
        m_viewMatrix.setToIdentity();
        m_viewMatrix.lookAt(m_position, m_target, m_up);
    }

    void updateProjectionMatrix() {
        m_projectionMatrix.setToIdentity();
        m_projectionMatrix.perspective(m_fov, m_aspectRatio, m_nearPlane, m_farPlane);
    }

    QVector3D m_position;
    QVector3D m_target;
    QVector3D m_up;
    
    float m_fov;
    float m_aspectRatio;
    float m_nearPlane;
    float m_farPlane;
    float m_distance;
    float m_rotationX;
    float m_rotationY;
    float m_sceneRadius = 1.0f; // 场景包围球半径，用于计算合理的最小距离
    
    QMatrix4x4 m_viewMatrix;
    QMatrix4x4 m_projectionMatrix;
};

#endif // CAMERA_HPP
