// Cones.hlsl
#include "Shared.hlsli"

//====================== ВЕРШИННЫЙ ШЕЙДЕР ======================

VSOut VSMain(VSInput v, uint instId : SV_InstanceID)
{
    uint idx = gBaseInstance + instId;
    InstanceData inst = gInstances[idx];

    float4 wp = mul(float4(v.pos, 1.0), inst.world);
    float3 wn = mul((float3x3) inst.world, v.nrm);

    VSOut o;
    o.pos = mul(wp, viewProj);
    o.nrmW = wn;
    o.posW = wp.xyz;
    o.obj = v.pos;
    o.tag = inst.tag.x;
    return o;
}

//====================== МОДЕЛЬ БЛИНН–ФОНГА ====================

float3 BlinnPhong(float3 N, float3 V, float3 L,
                  float3 lightColor, float3 albedo,
                  float shininess, float specScale)
{
    float ndotl = saturate(dot(N, L));
    if (ndotl <= 0.0f)
        return 0.0.xxx;

    float3 H = normalize(L + V);
    float ndoth = saturate(dot(N, H));

    float diffuse = ndotl;
    float specular = pow(ndoth, shininess) * specScale;

    float3 diffCol = albedo * lightColor * diffuse;
    float3 specCol = lightColor * specular;

    return diffCol + specCol;
}

//====================== ПИКСЕЛЬНЫЙ ШЕЙДЕР =====================

float4 PSMain(VSOut i) : SV_TARGET
{
    // ---------- Маркеры источников (кружочки) ----------
    if (i.tag >= 2.0f)
    {
        float3 lightColor;

        if (i.tag < 2.5f)        // направленный
            lightColor = float3(1.0, 1.0, 0.2); // жёлтый
        else if (i.tag < 3.5f)   // прожектор 0
            lightColor = float3(1.0, 0.3, 0.3); // красный
        else // прожектор 1
            lightColor = float3(0.3, 0.6, 1.0); // синий

        // плоский квадрат [-1;1] в XZ, рисуем в нём круг
        float2 uv = i.obj.xz;
        float r = length(uv);

        float mask = step(r, 1.0); // 1 внутри радиуса, 0 снаружи

        float3 bg = float3(0.2, 0.2, 0.2); // фон как clearColor
        float3 col = lerp(bg, lightColor, mask);

        return float4(col, 1.0);
    }

    // ---------- Остальное: конусы и пол под освещением ----------
    float3 N = normalize(i.nrmW);
    float3 V = normalize(camPos - i.posW);

    // --- материал (albedo) ---
    float3 albedo;
    bool isFloor = (i.tag > 0.5f && i.tag < 1.5f);

    if (isFloor)   // пол
    {
        float2 p = i.obj.xz * 10.0; // более мелкие клетки
        float2 cell = floor(p);
        float check = fmod(abs(cell.x + cell.y), 2.0);

        float3 c0 = float3(0.15, 0.15, 0.15);
        float3 c1 = float3(0.90, 0.90, 0.90);
        albedo = lerp(c0, c1, check);
    }
    else // конусы
    {
        const float3 base = float3(0.93, 0.35, 0.15);
        const float3 stripeCol = float3(0.05, 0.05, 0.95);

        float ang = atan2(i.obj.z, i.obj.x);
        float stripeWidth = 0.15;
        float m = smoothstep(1.0 - stripeWidth, 1.0, cos(ang));

        albedo = lerp(base, stripeCol, m);
    }

    // --- параметры блика для материалов ---
    float shininess = isFloor ? 16.0f : 32.0f;
    float specScaleDir = isFloor ? 0.0f : 1.0f; // пол — без блика от направленного света

    // --- освещение ---
    float3 color = ambientColor * albedo;

    // 1) направленный свет
    {
        float3 Ld = normalize(-dirLightDir);
        color += BlinnPhong(N, V, Ld, dirLightColor, albedo,
                            shininess, specScaleDir);
    }

    // 2) прожекторы
    [unroll]
    for (int k = 0; k < 2; ++k)
    {
        float3 lightPos = spotPosRange[k].xyz;
        float attK = spotPosRange[k].w;

        float3 spotDir = normalize(spotDirInner[k].xyz);
        float cosInner = spotDirInner[k].w;

        float3 lightCol = spotColorOuter[k].xyz;
        float cosOuter = spotColorOuter[k].w;

        // немного различим яркость: 0-й ярче, 1-й слабее
        float brightness = (k == 0) ? 1.2f : 0.5f;
        lightCol *= brightness;

        // направление из источника к точке
        float3 Lp = normalize(i.posW - lightPos);
        float3 L = -Lp;

        // угол между направлением прожектора и лучом к точке
        float cosTheta = dot(spotDir, Lp);
        float spotFactor = saturate((cosTheta - cosOuter) / (cosInner - cosOuter));

        // сужаем пятно (чтобы светило "под собой")
        spotFactor = pow(spotFactor, 4.0f);

        if (spotFactor <= 0.0f)
            continue;

        float dist = length(lightPos - i.posW);
        float att = 1.0f / (1.0f + attK * dist * dist); // 1 / (1 + k * d^2)

        // у пола блик от прожектора слабее, чем у конусов
        float specScaleSpot = isFloor ? 0.25f : 0.8f;
        float3 lp = BlinnPhong(N, V, L, lightCol, albedo,
                               shininess, specScaleSpot);

        color += lp * (spotFactor * att);
    }

    return float4(color, 1.0);
}
