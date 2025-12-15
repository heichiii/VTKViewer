#version 430 core

in vec3 vNormal;
in vec3 vPosition;
in float vScalar;

uniform vec3 lightDir;
uniform vec3 solidColor;
uniform int physicalData; // 0: solid, 1: point data, 2: cell data, 3: normal
uniform int colorMode;
uniform float scalarMin;
uniform float scalarMax;
uniform int twoSidedLighting; // 0: 单面, 1: 双面
uniform int renderPoints;     // 0: 三角形, 1: 点渲染

out vec4 fragColor;

// 精确的Viridis colormap - 使用9个控制点线性插值
vec3 viridis(float t) {
    t = clamp(t, 0.0, 1.0);
    
    // Viridis颜色控制点 (更准确的颜色)
    const vec3 c0 = vec3(0.267, 0.004, 0.329);  // 深紫色 t=0
    const vec3 c1 = vec3(0.282, 0.140, 0.458);  // 紫色   t=0.125
    const vec3 c2 = vec3(0.254, 0.265, 0.530);  // 蓝紫   t=0.25
    const vec3 c3 = vec3(0.207, 0.372, 0.553);  // 蓝色   t=0.375
    const vec3 c4 = vec3(0.164, 0.471, 0.558);  // 青蓝   t=0.5
    const vec3 c5 = vec3(0.128, 0.567, 0.551);  // 青色   t=0.625
    const vec3 c6 = vec3(0.267, 0.679, 0.480);  // 青绿   t=0.75
    const vec3 c7 = vec3(0.478, 0.821, 0.318);  // 黄绿   t=0.875
    const vec3 c8 = vec3(0.993, 0.906, 0.144);  // 亮黄   t=1
    
    // 分段线性插值
    float segment = t * 8.0;
    int idx = int(floor(segment));
    float f = fract(segment);
    
    if (idx >= 8) return c8;
    
    vec3 colors[9] = vec3[9](c0, c1, c2, c3, c4, c5, c6, c7, c8);
    return mix(colors[idx], colors[idx + 1], f);
}

// 可选：Jet colormap (更明显的颜色差异)
vec3 jet(float t) {
    t = clamp(t, 0.0, 1.0);
    
    vec3 color;
    if (t < 0.125) {
        color = vec3(0.0, 0.0, 0.5 + t * 4.0);
    } else if (t < 0.375) {
        color = vec3(0.0, (t - 0.125) * 4.0, 1.0);
    } else if (t < 0.625) {
        color = vec3((t - 0.375) * 4.0, 1.0, 1.0 - (t - 0.375) * 4.0);
    } else if (t < 0.875) {
        color = vec3(1.0, 1.0 - (t - 0.625) * 4.0, 0.0);
    } else {
        color = vec3(1.0 - (t - 0.875) * 4.0, 0.0, 0.0);
    }
    return clamp(color, 0.0, 1.0);
}

// Rainbow colormap
vec3 rainbow(float t) {
    t = clamp(t, 0.0, 1.0);
    float h = t * 5.0;  // 色相从0到5 (红->黄->绿->青->蓝->紫)
    float c = 1.0;
    float x = c * (1.0 - abs(mod(h, 2.0) - 1.0));
    
    vec3 rgb;
    if (h < 1.0)      rgb = vec3(c, x, 0.0);
    else if (h < 2.0) rgb = vec3(x, c, 0.0);
    else if (h < 3.0) rgb = vec3(0.0, c, x);
    else if (h < 4.0) rgb = vec3(0.0, x, c);
    else              rgb = vec3(x, 0.0, c);
    
    return rgb;
}

void main()
{
    // 圆形点渲染：丢弃圆形外的像素
    if (renderPoints == 1) {
        vec2 coord = gl_PointCoord * 2.0 - 1.0;  // 转换到 [-1, 1] 范围
        float dist = dot(coord, coord);
        if (dist > 1.0) {
            discard;  // 丢弃圆形外的像素
        }
        // 可选：添加边缘抗锯齿
        float alpha = 1.0 - smoothstep(0.8, 1.0, dist);
        
        vec3 baseColor;
        if (physicalData == 0) {
            baseColor = solidColor;
        } else if (physicalData == 1 || physicalData == 2) {
            if(colorMode==0)
                {baseColor = viridis(vScalar);}
            else if(colorMode==1)
                {baseColor = jet(vScalar);}
            else if(colorMode==2)
                {baseColor = rainbow(vScalar);}
            else
            {baseColor = viridis(vScalar);}
            // baseColor=viridis(vScalar);
        } else if (physicalData == 3) {
            baseColor = abs(normalize(vNormal)) * 0.5 + 0.5;
        } else {
            baseColor = solidColor;
        }
        fragColor = vec4(baseColor, alpha);
        return;
    }
    
    vec3 N = normalize(vNormal);
    vec3 L = normalize(lightDir);
    
    // 双面光照
    if (twoSidedLighting == 1 && !gl_FrontFacing) {
        N = -N;
    }
    
    float NdotL = max(dot(N, L), 0.0);
    
    float ambient = 0.2;
    float diffuse = 0.7 * NdotL;
    
    // 背面稍微亮一点
    if (twoSidedLighting == 1 && !gl_FrontFacing) {
        ambient = 0.3;
    }
    
    vec3 baseColor;
    
    if (physicalData == 0) {
        baseColor = solidColor;
    } else if (physicalData == 1 || physicalData == 2) {
        // 使用更鲜明的colormap
        if(colorMode==0)
        {baseColor = viridis(vScalar);}
        else if(colorMode==1)
        {baseColor = jet(vScalar);}
        else if(colorMode==2)
        {baseColor = rainbow(vScalar);}
        else
        {baseColor = viridis(vScalar);}
        // baseColor=viridis(vScalar);
    } else if (physicalData == 3) {
        baseColor = abs(N) * 0.5 + 0.5;
    } else {
        baseColor = solidColor;
    }
    
    vec3 finalColor = baseColor * (ambient + diffuse);
    
    // 高光
    vec3 V = normalize(-vPosition);
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 64.0);
    finalColor += vec3(0.2) * spec;
    
    fragColor = vec4(finalColor, 1.0);
}
