#include "Shared.hlsli"

// Вершинный шейдер
VSOut VSMain(VSInput v, uint instId : SV_InstanceID)
{
    uint idx = gBaseInstance + instId;
    InstanceData inst = gInstances[idx];

    // Позиция и нормаль в мировом пространстве
    float4 wp = mul(float4(v.pos, 1.0f), inst.world);
    float3 wn = mul((float3x3) inst.world, v.nrm);

    VSOut o;
    o.pos = mul(wp, viewProj);
    o.nrmW = wn;
    o.posW = wp.xyz;
    o.obj = v.pos;
    o.tag = inst.tag.x;

    o.matAlbedo = inst.mat.albedo;
    o.matSpec = inst.mat.specColor;
    o.matShin = inst.mat.shininess;

    return o;
}

// Модель Блинна–Фонга
float3 BlinnPhong(
    float3 N,
    float3 V,
    float3 L,
    float3 lightColor,
    float3 albedo,
    float3 specColor,
    float shininess,
    float specScale)
{
    float ndotl = saturate(dot(N, L));
    if (ndotl <= 0.0f)
        return float3(0.0, 0.0, 0.0);

    float3 H = normalize(L + V);
    float ndoth = saturate(dot(N, H));

    float diffuse = ndotl;
    float specular = pow(ndoth, shininess);

    float3 diffCol = albedo * lightColor * diffuse;
    float3 specCol = specColor * lightColor * (specular * specScale);

    return diffCol + specCol;
}

// Пиксельный шейдер
float4 PSMain(VSOut i) : SV_TARGET
{
    // Маркеры источников (кружочки)
    if (i.tag >= 2.0f)
    {
        float3 lightColor;

        if (i.tag < 2.5f)          // направленный свет
        {
            lightColor = float3(1.0, 1.0, 0.2); // жёлтый
        }
        else if (i.tag < 3.5f)     // прожектор 0
        {
            lightColor = float3(1.0, 0.3, 0.3); // красный
        }
        else // прожектор 1
        {
            lightColor = float3(0.3, 0.6, 1.0); // синий
        }

        // Плоский квадрат [-1; 1] в XZ, рисуем в нём круг
        float2 uv = i.obj.xz;
        float r = length(uv);
        float mask = step(r, 1.0); // 1 внутри радиуса, 0 снаружи

        float3 bg = float3(0.2, 0.2, 0.2);
        float3 col = lerp(bg, lightColor, mask);

        return float4(col, 1.0);
    }

    // Конусы и пол под освещением
    float3 N = normalize(i.nrmW);
    float3 V = normalize(camPos - i.posW);

    bool isFloor = (i.tag > 0.5f && i.tag < 1.5f);
    bool isCone = (i.tag < 0.5f);

    float3 matAlbedo = i.matAlbedo;
    float3 matSpec = i.matSpec;
    float shininess = i.matShin;

    // Альбедо с учётом узора
    float3 albedo;

    if (isFloor)
    {
        // Шахматка в объектном пространстве пола
        float2 p = i.obj.xz * 10.0; // мелкие клетки
        float2 cell = floor(p);
        float check = fmod(abs(cell.x + cell.y), 2.0);

        float3 c0 = matAlbedo * 0.25; // тёмная клетка
        float3 c1 = matAlbedo; // светлая клетка
        albedo = lerp(c0, c1, check);
    }
    else
    {
        // Конусы: базовый цвет + синяя полоса
        float3 base = matAlbedo;
        float3 stripeCol = matAlbedo * float3(0.10, 0.10, 2.50);

        float ang = atan2(i.obj.z, i.obj.x);
        float stripeWidth = 0.15;
        float m = smoothstep(1.0f - stripeWidth, 1.0f, cos(ang));

        albedo = lerp(base, stripeCol, m);
    }

    // Базовый ambient
    float3 color = ambientColor * albedo;

    // Направленный свет
    {
        float3 Ld = normalize(-dirLightDir);
        float specScaleDir = isFloor ? 0.2f : 0.7f;

        color += BlinnPhong(
            N, V, Ld,
            dirLightColor,
            albedo, matSpec,
            shininess, specScaleDir);
    }

    // Точечные источники
    [loop]
    for (uint k = 0; k < gNumPointLights; ++k)
    {
        PointLight pl = gPointLights[k];

        float3 Lvec = pl.pos - i.posW;
        float dist = length(Lvec);
        float3 L = Lvec / max(dist, 1e-4f);

        float att = 1.0f / (1.0f + pl.attK * dist * dist);

        float specScalePoint = isFloor ? 0.25f : 0.8f;

        float3 contrib = BlinnPhong(
            N, V, L,
            pl.color,
            albedo, matSpec,
            shininess, specScalePoint);

        color += contrib * att;
    }

    // Прожекторы
    [loop]
    for (uint k = 0; k < gNumSpotLights; ++k)
    {
        SpotLight sl = gSpotLights[k];

        float3 Lvec = sl.pos - i.posW;
        float dist = length(Lvec);
        float3 L = Lvec / max(dist, 1e-4f);

        // Направление из источника к точке
        float3 Lp = -L;
        float cosTheta = dot(sl.dir, Lp);

        float spotFactor = saturate(
            (cosTheta - sl.cosOuter) /
            (sl.cosInner - sl.cosOuter));
        spotFactor = pow(spotFactor, 4.0f); // мягкие края

        if (spotFactor <= 0.0f)
            continue;

        float att = 1.0f / (1.0f + sl.attK * dist * dist);

        float specScaleSpot = isFloor ? 0.25f : 0.8f;

        float3 contrib = BlinnPhong(
            N, V, L,
            sl.color,
            albedo, matSpec,
            shininess, specScaleSpot);

        color += contrib * (spotFactor * att);
    }

    return float4(color, 1.0);
}
