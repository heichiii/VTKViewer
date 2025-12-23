# 🔍 VTK查看器项目完整流程详解

---

## **一、程序入口与初始化 (main.cpp)**

### 1. **应用启动**
- **设置GPU策略**：强制使用独立显卡（NVIDIA/AMD），通过导出符号 `NvOptimusEnablement` 和 `AmdPowerXpressRequestHighPerformance`
- **创建Qt应用**：使用桌面OpenGL模式 (`Qt::AA_UseDesktopOpenGL`)，避免使用ANGLE
- **创建主窗口**：实例化 `MainWindow`，包含OpenGL渲染窗口和用户界面

---

## **二、数据加载流程 (Loader模块)**

### 2. **VTK文件读取 (VTKLegacyLoader)**

#### 2.1 内存映射文件
- **Windows**：使用 `CreateFileA` → `CreateFileMappingA` → `MapViewOfFile` 将文件映射到内存
- **Linux**：使用 `mmap` 系统调用
- **优势**：高效读取大文件，直接内存访问，无需拷贝数据到缓冲区

#### 2.2 解析文件头
```cpp
// 读取4行信息
1. "# vtk DataFile Version 5.1"  -> 提取版本号
2. "Title line"                   -> 文件标题
3. "ASCII" 或 "BINARY"           -> 数据格式
4. "DATASET UNSTRUCTURED_GRID"   -> 数据集类型
```

#### 2.3 解析几何数据结构

**DataArray 数据结构**（Loader.hpp#L13-L32）：
```cpp
struct DataArray {
    std::string name;           // 数组名称
    int64_t num_components;     // 每个数据元组的分量数（标量=1，向量=3）
    int64_t num_tuples;         // 数据元组总数
    std::string data_type;      // 数据类型：float/double/int
    
    // 根据类型选择一个激活的存储容器
    std::vector<float> data_float;
    std::vector<double> data_double;
    std::vector<int32_t> data_int32;
    std::vector<int64_t> data_int64;
};
```

**UnstructuredGrid 结构**（Loader.hpp#L35-L48）：
```cpp
struct UnstructuredGrid {
    int64_t num_points;                                    // 点数量
    int64_t num_cells;                                     // 单元数量
    
    std::shared_ptr<DataArray> points;                     // 点坐标（3分量float/double）
    std::vector<int32_t> cells;                           // 单元拓扑：[n, id1, id2, ..., n, id1, ...]
    std::vector<uint8_t> cell_types;                      // 单元类型（VTK_TRIANGLE, VTK_TETRA等）
    
    std::map<std::string, std::shared_ptr<DataArray>> point_data;  // 点属性数据
    std::map<std::string, std::shared_ptr<DataArray>> cell_data;   // 单元属性数据
};
```

#### 2.4 解析数据段
```
POINTS 8 float
  x0 y0 z0  x1 y1 z1  x2 y2 z2  ...
  -> 存入 grid_.points 的 data_float 向量

CELLS 6 30
  4 0 1 2 3  4 4 5 6 7  ...  (格式：点数 索引列表)
  -> 存入 grid_.cells

CELL_TYPES 6
  12 12 12 12 12 12  (12=VTK_HEXAHEDRON)
  -> 存入 grid_.cell_types

POINT_DATA 8
SCALARS temperature float 1
  0.5 0.8 1.2 0.3 ...
  -> 存入 grid_.point_data["temperature"]

CELL_DATA 6
VECTORS velocity float
  vx0 vy0 vz0  vx1 vy1 vz1 ...
  -> 存入 grid_.cell_data["velocity"]
```

---

## **三、数据处理流程 (MeshProcessor)**

### 3. **网格预处理 - 提取表面**

#### 3.1 提取点坐标（MeshProcessor.cpp#L35-L52）
```cpp
// 从DataArray复制所有点到临时缓冲区
std::vector<float> positions(numPoints * 3);
for (size_t i = 0; i < numPoints; ++i) {
    positions[i*3+0] = points->data_float[i*numComp+0];  // X
    positions[i*3+1] = points->data_float[i*numComp+1];  // Y
    positions[i*3+2] = points->data_float[i*numComp+2];  // Z
}
```

#### 3.2 计算包围盒
- 遍历所有点，找到 `min(x,y,z)` 和 `max(x,y,z)`
- 用于后续相机自动聚焦

#### 3.3 提取所有面（MeshProcessor.cpp#L58-L172）

**Face 数据结构**（MeshProcessor.hpp#L36-L82）：
```cpp
struct Face {
    uint32_t sorted[4];    // 排序后的索引（用于比较唯一性）
    uint32_t orig[4];      // 原始索引（保持绕序，用于渲染）
    uint32_t cellIdx;      // 所属单元索引
    uint8_t n;             // 顶点数（3=三角形，4=四边形）
};
```

**提取逻辑**：
```cpp
// 遍历每个单元，根据类型提取所有面
switch (cellType) {
    case VTK_TRIANGLE:     // 1个三角形面
        face.set3(idx0, idx1, idx2);
        
    case VTK_QUAD:         // 1个四边形面
        face.set4(idx0, idx1, idx2, idx3);
        
    case VTK_TETRA:        // 4个三角形面
        提取4个三角形面（每个面3个顶点）
        
    case VTK_HEXAHEDRON:   // 6个四边形面
        提取6个四边形面（立方体6个面）
        
    case VTK_PYRAMID:      // 1个四边形底+4个三角形面
        提取5个面
}
```

#### 3.4 排序与去重（MeshProcessor.cpp#L176-L192）
```cpp
// 并行排序所有面
PAR_SORT(allFaces.begin(), allFaces.end());  // 使用C++17并行算法

// 提取边界面（只出现1次的面）
while (i < nFaces) {
    // 统计相同面的数量
    if (count == 1) {
        // 内部面出现2次，边界面出现1次
        boundaryFaces.push_back(&allFaces[i]);
    }
}
```

#### 3.5 生成GPU顶点数据（平面着色）（MeshProcessor.cpp#L194-L288）

**GPUMeshData 结构**（MeshProcessor.hpp#L8-L30）：
```cpp
struct GPUMeshData {
    // 交错顶点数据：[x,y,z, nx,ny,nz, scalar] * N
    std::vector<float> vertexData;  // 每顶点7个float
    
    std::vector<uint32_t> triangleIndices;  // 三角形索引
    std::vector<uint32_t> lineIndices;      // 线框索引
    std::vector<uint32_t> pointIndices;     // 点索引
    
    // 映射关系
    std::vector<uint32_t> vertexToPointIndex;  // 渲染顶点 -> 原始点
    std::vector<uint32_t> vertexToCellIndex;   // 渲染顶点 -> 原始单元
    
    QVector3D boundingBoxMin, boundingBoxMax;
    float scalarMin, scalarMax;
    bool useFlatShading;  // 平面着色标志
};
```

**顶点生成逻辑**：
```cpp
auto emitTriangle = [&](uint32_t i0, uint32_t i1, uint32_t i2, uint32_t cellIdx) {
    // 1. 获取3个顶点的位置
    vec3 v0 = positions[i0];
    vec3 v1 = positions[i1];
    vec3 v2 = positions[i2];
    
    // 2. 计算面法线（叉积）
    vec3 edge1 = v1 - v0;
    vec3 edge2 = v2 - v0;
    vec3 normal = normalize(cross(edge1, edge2));
    
    // 3. 生成3个独立顶点（平面着色：同一三角形共享法线）
    for (i = 0; i < 3; i++) {
        vertexData[vertIdx * 7 + 0..2] = position[i];  // xyz
        vertexData[vertIdx * 7 + 3..5] = normal;       // 法线
        vertexData[vertIdx * 7 + 6] = 0.5f;            // 默认标量值
        
        vertexToPointIndex[vertIdx] = originalPointIndex[i];
        vertexToCellIndex[vertIdx] = cellIdx;
        vertIdx++;
    }
    
    // 4. 生成线框索引（边）
    lineIndices.push_back({v0, v1, v1, v2, v2, v0});
};

// 遍历所有边界面
for (Face* face : boundaryFaces) {
    if (face->n == 3) {
        emitTriangle(face->orig[0], face->orig[1], face->orig[2]);
    } else if (face->n == 4) {
        // 四边形分为2个三角形
        emitTriangle(face->orig[0], face->orig[1], face->orig[2]);
        emitTriangle(face->orig[0], face->orig[2], face->orig[3]);
    }
}
```

#### 3.6 更新标量数据（MeshProcessor.cpp#L355-L458）
```cpp
void updateScalars(GPUMeshData& meshData, const std::string& arrayName, bool isPointData) {
    // 1. 获取数据数组
    DataArray* array = isPointData ? grid->point_data[arrayName] : grid->cell_data[arrayName];
    
    // 2. 处理向量数据：计算模长
    auto getScalar = [&](size_t tupleIdx) -> float {
        if (numComponents == 1) {
            return array->data_float[tupleIdx];  // 标量直接返回
        } else {
            // 向量：计算 sqrt(x^2 + y^2 + z^2)
            float sumSq = 0;
            for (int c = 0; c < numComponents; c++) {
                sumSq += array->data[tupleIdx*numComponents + c]^2;
            }
            return sqrt(sumSq);
        }
    };
    
    // 3. 计算数据范围
    float minVal = min(all scalars);
    float maxVal = max(all scalars);
    meshData.scalarMin = minVal;
    meshData.scalarMax = maxVal;
    
    // 4. 归一化到[0,1]并更新顶点数据
    for (size_t v = 0; v < numVertices; v++) {
        uint32_t origIdx = meshData.vertexToPointIndex[v];  // 或 vertexToCellIndex
        float scalar = (getScalar(origIdx) - minVal) / (maxVal - minVal);
        meshData.vertexData[v * 7 + 6] = scalar;  // 更新第7个分量
    }
}
```

---

## **四、OpenGL渲染流程 (GLWidget)**

### 4. **OpenGL初始化**（GLWidget.cpp#L35-L86）

```cpp
void initializeGL() {
    initializeOpenGLFunctions();  // 加载OpenGL 4.3 Core函数指针
    
    // 1. 配置渲染状态
    glClearColor(0.15f, 0.15f, 0.18f, 1.0f);  // 背景色
    glEnable(GL_DEPTH_TEST);                   // 深度测试
    glDepthFunc(GL_LESS);
    
    glEnable(GL_CULL_FACE);                    // 背面剔除
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);                       // 逆时针为正面
    
    glEnable(GL_MULTISAMPLE);                  // 多重采样抗锯齿
    glEnable(GL_BLEND);                        // 混合（透明度）
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    glEnable(GL_PROGRAM_POINT_SIZE);           // 允许着色器控制点大小
    glEnable(GL_POLYGON_OFFSET_FILL);          // 多边形偏移（防Z-fighting）
    glPolygonOffset(1.0f, 1.0f);
    
    // 2. 加载着色器程序
    setupShaders();
    
    // 3. 创建缓冲区对象
    setupBuffers();
}
```

### 5. **着色器加载**（GLWidget.cpp#L120-L147）

```cpp
void setupShaders() {
    // 主网格着色器
    m_meshShader = new QOpenGLShaderProgram();
    m_meshShader->addShaderFromSourceFile(QOpenGLShader::Vertex, ":/shaders/mesh.vert");
    m_meshShader->addShaderFromSourceFile(QOpenGLShader::Fragment, ":/shaders/mesh.frag");
    m_meshShader->link();
    
    // 缓存uniform位置（性能优化）
    m_mvpLoc = m_meshShader->uniformLocation("mvp");
    m_modelViewLoc = m_meshShader->uniformLocation("modelView");
    m_normalMatrixLoc = m_meshShader->uniformLocation("normalMatrix");
    m_lightDirLoc = m_meshShader->uniformLocation("lightDir");
    m_physicalDataLoc = m_meshShader->uniformLocation("physicalData");
    m_colorModeLoc = m_meshShader->uniformLocation("colorMode");
    
    // 线框着色器（简单的单色）
    m_wireShader->addShaderFromSourceFile(...);
    
    // 坐标轴着色器
    m_axesShader->addShaderFromSourceFile(...);
}
```

### 6. **缓冲区对象创建**（GLWidget.cpp#L149-L190）

```cpp
void setupBuffers() {
    // 创建VAO（顶点数组对象）
    m_meshVAO.create();
    m_meshVAO.bind();
    
    // VBO（顶点缓冲对象）
    m_vertexBuffer.create();
    m_vertexBuffer.setUsagePattern(QOpenGLBuffer::DynamicDraw);  // 数据会更新
    
    // 索引缓冲对象
    m_triangleIndexBuffer.create();
    m_lineIndexBuffer.create();
    m_pointIndexBuffer.create();
    
    m_meshVAO.release();
    
    // 坐标轴VAO和VBO
    m_axesVAO.create();
    m_axesBuffer.create();
    float axesVertices[] = {
        // X轴（红色）
        0,0,0, 1,0,0,   1,0,0, 1,0,0,
        // Y轴（绿色）
        0,0,0, 0,1,0,   0,1,0, 0,1,0,
        // Z轴（蓝色）
        0,0,0, 0,0,1,   0,0,1, 0,0,1,
    };
    m_axesBuffer.allocate(axesVertices, sizeof(axesVertices));
}
```

### 7. **缓冲区数据上传**（GLWidget.cpp#L192-L245）

```cpp
void updateBuffers() {
    m_meshVAO.bind();
    
    // 1. 上传顶点数据
    m_vertexBuffer.bind();
    m_vertexBuffer.allocate(m_meshData.vertexData.data(), 
                            m_meshData.vertexData.size() * sizeof(float));
    
    // 2. 配置顶点属性指针
    // Location 0: 位置 (3 floats)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7*sizeof(float), (void*)0);
    
    // Location 1: 法线 (3 floats)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 7*sizeof(float), (void*)(3*sizeof(float)));
    
    // Location 2: 标量 (1 float)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 7*sizeof(float), (void*)(6*sizeof(float)));
    
    // 3. 上传索引数据
    m_triangleIndexBuffer.bind();
    m_triangleIndexBuffer.allocate(m_meshData.triangleIndices.data(), ...);
    
    m_lineIndexBuffer.bind();
    m_lineIndexBuffer.allocate(m_meshData.lineIndices.data(), ...);
    
    m_pointIndexBuffer.bind();
    m_pointIndexBuffer.allocate(m_meshData.pointIndices.data(), ...);
    
    m_meshVAO.release();
}
```

---

## **五、渲染管线 (着色器)**

### 8. **顶点着色器 (mesh.vert)**（Resources/shaders/mesh.vert#L1-L24）

```glsl
#version 430 core

// 输入顶点属性（来自VBO）
layout(location = 0) in vec3 aPosition;  // 位置
layout(location = 1) in vec3 aNormal;    // 法线
layout(location = 2) in float aScalar;   // 标量值（归一化0-1）

// Uniform变量（来自CPU）
uniform mat4 mvp;           // 模型-视图-投影矩阵
uniform mat4 modelView;     // 模型-视图矩阵
uniform mat3 normalMatrix;  // 法线矩阵（modelView的逆转置）
uniform float pointSize;    // 点大小

// 输出到片段着色器
out vec3 vNormal;    // 观察空间法线
out vec3 vPosition;  // 观察空间位置
out float vScalar;   // 传递标量值

void main()
{
    // 1. 变换位置到观察空间
    vec4 viewPos = modelView * vec4(aPosition, 1.0);
    vPosition = viewPos.xyz;
    
    // 2. 变换法线到观察空间（需要特殊矩阵处理非均匀缩放）
    vNormal = normalize(normalMatrix * aNormal);
    
    // 3. 传递标量值
    vScalar = aScalar;
    
    // 4. 计算裁剪空间位置（最终顶点位置）
    gl_Position = mvp * vec4(aPosition, 1.0);
    
    // 5. 设置点渲染大小
    gl_PointSize = pointSize;
}
```

**关键概念**：
- **MVP矩阵**：Model-View-Projection，将顶点从局部空间 → 世界空间 → 观察空间 → 裁剪空间
- **法线矩阵**：`normalMatrix = transpose(inverse(mat3(modelView)))`，确保法线在非均匀缩放下正确变换
- **观察空间**：以相机为原点的坐标系，用于光照计算

### 9. **片段着色器 (mesh.frag)**（Resources/shaders/mesh.frag#L1-L163）

```glsl
#version 430 core

// 从顶点着色器插值而来的输入
in vec3 vNormal;    // 观察空间法线（自动插值）
in vec3 vPosition;  // 观察空间位置
in float vScalar;   // 标量值

// Uniform参数
uniform vec3 lightDir;       // 光照方向
uniform vec3 solidColor;     // 纯色
uniform int physicalData;    // 0=纯色, 1=点数据, 2=单元数据, 3=法线颜色
uniform int colorMode;       // 0=Viridis, 1=Jet, 2=Rainbow
uniform float scalarMin;     // 标量最小值
uniform float scalarMax;     // 标量最大值
uniform int twoSidedLighting;// 双面光照
uniform int renderPoints;    // 点渲染模式

out vec4 fragColor;  // 最终颜色输出

// Viridis色彩映射函数
vec3 viridis(float t) {
    t = clamp(t, 0.0, 1.0);
    
    // 9个控制点的线性插值
    const vec3 colors[9] = {
        vec3(0.267, 0.004, 0.329),  // 深紫
        vec3(0.282, 0.140, 0.458),  // 紫色
        vec3(0.254, 0.265, 0.530),  // 蓝紫
        vec3(0.207, 0.372, 0.553),  // 蓝色
        vec3(0.164, 0.471, 0.558),  // 青蓝
        vec3(0.128, 0.567, 0.551),  // 青色
        vec3(0.267, 0.679, 0.480),  // 青绿
        vec3(0.478, 0.821, 0.318),  // 黄绿
        vec3(0.993, 0.906, 0.144)   // 亮黄
    };
    
    float segment = t * 8.0;
    int idx = int(floor(segment));
    float f = fract(segment);
    
    return mix(colors[idx], colors[idx+1], f);
}

// Jet色彩映射（蓝→青→绿→黄→红）
vec3 jet(float t) {
    t = clamp(t, 0.0, 1.0);
    if (t < 0.125) return vec3(0.0, 0.0, 0.5 + t*4.0);
    else if (t < 0.375) return vec3(0.0, (t-0.125)*4.0, 1.0);
    else if (t < 0.625) return vec3((t-0.375)*4.0, 1.0, 1.0-(t-0.375)*4.0);
    else if (t < 0.875) return vec3(1.0, 1.0-(t-0.625)*4.0, 0.0);
    else return vec3(1.0-(t-0.875)*4.0, 0.0, 0.0);
}

void main()
{
    // === 点渲染模式：绘制圆形点 ===
    if (renderPoints == 1) {
        // gl_PointCoord: [0,1] x [0,1]，转换到 [-1,1] x [-1,1]
        vec2 coord = gl_PointCoord * 2.0 - 1.0;
        float dist = dot(coord, coord);  // 距离中心的平方距离
        
        // 丢弃圆形外的像素
        if (dist > 1.0) discard;
        
        // 边缘抗锯齿
        float alpha = 1.0 - smoothstep(0.8, 1.0, dist);
        
        // 根据物理数据模式确定颜色
        vec3 baseColor = (physicalData == 0) ? solidColor 
                       : (physicalData == 1 || physicalData == 2) ? viridis(vScalar)
                       : abs(normalize(vNormal)) * 0.5 + 0.5;
        
        fragColor = vec4(baseColor, alpha);
        return;
    }
    
    // === 三角形渲染模式：Phong光照 ===
    
    // 1. 归一化法线
    vec3 N = normalize(vNormal);
    vec3 L = normalize(lightDir);
    
    // 2. 双面光照：如果是背面，翻转法线
    if (twoSidedLighting == 1 && !gl_FrontFacing) {
        N = -N;
    }
    
    // 3. 漫反射光照（Lambertian）
    float NdotL = max(dot(N, L), 0.0);
    float ambient = 0.2;   // 环境光
    float diffuse = 0.7 * NdotL;  // 漫反射
    
    // 4. 确定基础颜色
    vec3 baseColor;
    if (physicalData == 0) {
        baseColor = solidColor;  // 纯色模式
    } else if (physicalData == 1 || physicalData == 2) {
        // 使用色彩映射
        baseColor = (colorMode == 0) ? viridis(vScalar)
                  : (colorMode == 1) ? jet(vScalar)
                  : viridis(vScalar);  // 默认Viridis
    } else if (physicalData == 3) {
        baseColor = abs(N) * 0.5 + 0.5;  // 法线可视化
    } else {
        baseColor = solidColor;
    }
    
    // 5. 应用光照
    vec3 finalColor = baseColor * (ambient + diffuse);
    
    // 6. 添加镜面高光（Blinn-Phong）
    vec3 V = normalize(-vPosition);       // 观察方向
    vec3 H = normalize(L + V);            // 半程向量
    float spec = pow(max(dot(N, H), 0.0), 64.0);  // 高光强度
    finalColor += vec3(0.2) * spec;
    
    fragColor = vec4(finalColor, 1.0);
}
```

**关键光照计算**：
- **环境光（Ambient）**：`0.2 * baseColor`，模拟间接光照
- **漫反射（Diffuse）**：`0.7 * max(N·L, 0) * baseColor`，Lambert定律
- **镜面高光（Specular）**：`0.2 * (N·H)^64`，Blinn-Phong模型

---

## **六、渲染循环 (paintGL)**

### 10. **每帧渲染**（GLWidget.cpp#L250-L450）

```cpp
void paintGL() {
    // 1. 清除缓冲区
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // 2. 计算变换矩阵
    QMatrix4x4 model;                           // 模型矩阵（单位矩阵）
    QMatrix4x4 view = m_camera.getViewMatrix(); // 视图矩阵
    QMatrix4x4 proj = m_camera.getProjectionMatrix(); // 投影矩阵
    
    QMatrix4x4 mvp = proj * view * model;       // MVP矩阵
    QMatrix4x4 modelView = view * model;        // MV矩阵
    QMatrix3x3 normalMatrix = modelView.normalMatrix();  // 法线矩阵
    
    // 3. 绑定着色器和VAO
    m_meshShader->bind();
    m_meshVAO.bind();
    
    // 4. 传递uniform变量
    m_meshShader->setUniformValue(m_mvpLoc, mvp);
    m_meshShader->setUniformValue(m_modelViewLoc, modelView);
    m_meshShader->setUniformValue(m_normalMatrixLoc, normalMatrix);
    m_meshShader->setUniformValue(m_lightDirLoc, QVector3D(0.5, 1.0, 0.8));
    m_meshShader->setUniformValue(m_physicalDataLoc, m_physicalDataMode);
    m_meshShader->setUniformValue(m_colorModeLoc, m_colorMode);
    m_meshShader->setUniformValue("scalarMin", m_meshData.scalarMin);
    m_meshShader->setUniformValue("scalarMax", m_meshData.scalarMax);
    
    // 5. 根据渲染模式绘制
    if (m_renderMode == Solid) {
        glEnable(GL_CULL_FACE);  // 启用背面剔除
        m_triangleIndexBuffer.bind();
        glDrawElements(GL_TRIANGLES, m_meshData.triangleIndices.size(), 
                       GL_UNSIGNED_INT, nullptr);
    } 
    else if (m_renderMode == Wireframe) {
        glDisable(GL_CULL_FACE);  // 禁用剔除以显示所有边
        m_lineIndexBuffer.bind();
        glLineWidth(1.0f);
        glDrawElements(GL_LINES, m_meshData.lineIndices.size(), 
                       GL_UNSIGNED_INT, nullptr);
    }
    else if (m_renderMode == Points) {
        m_meshShader->setUniformValue("renderPoints", 1);
        glDrawArrays(GL_POINTS, 0, m_meshData.vertexCount);
    }
    else if (m_renderMode == SolidWireframe) {
        // 先绘制实体（带多边形偏移）
        glEnable(GL_POLYGON_OFFSET_FILL);
        m_triangleIndexBuffer.bind();
        glDrawElements(GL_TRIANGLES, ...);
        
        // 再绘制线框
        glDisable(GL_POLYGON_OFFSET_FILL);
        m_wireShader->bind();
        m_lineIndexBuffer.bind();
        glDrawElements(GL_LINES, ...);
    }
    
    m_meshVAO.release();
    m_meshShader->release();
    
    // 6. 绘制坐标轴
    renderAxes();
}
```

**OpenGL绘制命令**：
- **glDrawElements**：使用索引缓冲区绘制
  - `GL_TRIANGLES`：每3个索引形成一个三角形
  - `GL_LINES`：每2个索引形成一条线段
- **glDrawArrays**：顺序绘制顶点
  - `GL_POINTS`：每个顶点绘制一个点

---

## **七、用户交互 (Camera + 鼠标事件)**

### 11. **相机变换**（Camera.hpp#L1-L100）

```cpp
class Camera {
    QVector3D m_position;   // 相机位置
    QVector3D m_target;     // 观察目标
    QVector3D m_up;         // 上方向
    
    float m_distance;       // 到目标的距离
    float m_rotationX;      // 俯仰角
    float m_rotationY;      // 偏航角
    float m_fov;            // 视场角
    
    void updateViewMatrix() {
        // 球坐标转换为笛卡尔坐标
        float pitch = qDegreesToRadians(m_rotationX);
        float yaw = qDegreesToRadians(m_rotationY);
        
        QVector3D offset;
        offset.setX(m_distance * cos(pitch) * cos(yaw));
        offset.setY(m_distance * sin(pitch));
        offset.setZ(m_distance * cos(pitch) * sin(yaw));
        
        m_position = m_target + offset;
        
        // 构建视图矩阵
        m_viewMatrix.setToIdentity();
        m_viewMatrix.lookAt(m_position, m_target, m_up);
    }
    
    QMatrix4x4 getProjectionMatrix() {
        QMatrix4x4 proj;
        proj.perspective(m_fov, m_aspectRatio, m_nearPlane, m_farPlane);
        return proj;
    }
};
```

### 12. **鼠标事件处理**

```cpp
void GLWidget::mouseMoveEvent(QMouseEvent *event) {
    QPoint delta = event->pos() - m_lastMousePos;
    
    if (event->buttons() & Qt::LeftButton) {
        // 左键：旋转
        m_camera.rotate(delta.x(), delta.y());
    }
    else if (event->buttons() & Qt::MiddleButton) {
        // 中键：平移
        m_camera.pan(delta.x(), delta.y());
    }
    
    m_lastMousePos = event->pos();
    update();  // 触发重绘
}

void GLWidget::wheelEvent(QWheelEvent *event) {
    // 滚轮：缩放
    m_camera.zoom(event->angleDelta().y());
    update();
}
```

---

## **八、完整数据流总结**

```
VTK文件 (磁盘)
    ↓ [内存映射 mmap/MapViewOfFile]
内存中的原始字节流
    ↓ [VTKLegacyLoader::load()]
UnstructuredGrid {
    points: DataArray {
        data_float: [x0,y0,z0, x1,y1,z1, ...]
    }
    cells: [4,0,1,2,3, 8,4,5,6,7,8,9,10,11, ...]
    cell_types: [12, 12, ...]
    point_data: {"temperature" → DataArray}
    cell_data: {"pressure" → DataArray}
}
    ↓ [MeshProcessor::process()]
GPUMeshData {
    vertexData: [x,y,z,nx,ny,nz,s, x,y,z,nx,ny,nz,s, ...]  // 平面着色
    triangleIndices: [0,1,2, 3,4,5, ...]
    lineIndices: [0,1, 1,2, 2,0, ...]
    vertexToPointIndex: [原始点映射]
    vertexToCellIndex: [单元映射]
}
    ↓ [GLWidget::updateBuffers()]
GPU显存 {
    VBO: OpenGL缓冲区 (vertexData)
    EBO: 索引缓冲区 (triangleIndices, lineIndices)
    VAO: 顶点属性配置
}
    ↓ [paintGL() + glDrawElements()]
顶点着色器 {
    输入：位置、法线、标量
    输出：裁剪空间位置、观察空间法线、标量
}
    ↓ [光栅化 + 插值]
片段着色器 {
    光照计算：Phong模型
    色彩映射：Viridis/Jet
    输出：RGBA颜色
}
    ↓ [深度测试 + 混合]
屏幕像素 (帧缓冲)
    ↓ [交换缓冲区]
显示器 → 用户看到的画面
```

---

## **关键技术要点**

1. **平面着色（Flat Shading）**：每个三角形3个顶点共享相同法线，产生硬边缘效果
2. **面排序去重**：通过对所有面排序并统计出现次数，快速提取表面（边界面出现1次）
3. **交错顶点数据**：`[pos, normal, scalar]` 交错存储，GPU缓存友好
4. **索引绘制**：使用 `glDrawElements` 复用顶点，节省内存
5. **双面光照**：通过 `gl_FrontFacing` 判断背面并翻转法线
6. **色彩映射**：将标量值 `[0,1]` 映射到科学可视化色彩（Viridis/Jet）
7. **球坐标相机**：使用俯仰角+偏航角+距离实现轨道旋转

---

**总结**：这就是从VTK文件到可交互3D画面的完整流程！每个环节都有详细的数据结构和算法实现。
