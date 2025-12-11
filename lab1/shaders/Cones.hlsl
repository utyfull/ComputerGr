#include "Shared.hlsli"

// текстуры
Texture2D gDiffuseTex0 : register(t3); // куб 0
Texture2D gDiffuseTex1 : register(t4); // куб 1
Texture2D gShadowMap : register(t5); // shadow map

// s0 – обычные текстуры, s1 – shadow map
SamplerState gTextureSam : register(s0);
SamplerState gShadowSam : register(s1);

// ---------------- Вершинный шейдер основного прохода ----------------
VSOut VSMain(VSInput v, uint instId : SV_InstanceID)
{
    uint idx = gBaseInstance + instId;
    InstanceData inst = gInstances[idx];

    float4 wp = mul(float4(v.pos, 1.0f), inst.world);
    float3 wn = mul(v.nrm, (float3x3) inst.world);
    wn = normalize(wn);

    VSOut o;
    o.pos = mul(wp, viewProj);
    o.nrmW = wn;
    o.posW = wp.xyz;
    o.obj = v.pos;
    o.tag = inst.tag.x;

    o.matAlbedo = inst.mat.albedo;
    o.matSpec = inst.mat.specColor;
    o.matShin = inst.mat.shininess;
    o.uv = v.uv;

    return o;
}

// ---------------- Blinn–Phong ----------------
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

// ---------------- Shadow mapping ----------------
// worldPos – мировая позиция пикселя
// N – нормаль в мире
// Ld – нормализованное направление на источник (для dir light: -dirLightDir)
float ComputeShadow(float3 worldPos, float3 N, float3 Ld)
{
    // normal-offset, чтобы уменьшить self-shadowing
    float3 offsetPos = worldPos + N * 0.003f;

    // позиция в пространстве света
    float4 lp = mul(float4(offsetPos, 1.0f), lightViewProj);
    float3 proj = lp.xyz / lp.w;

    // если вне объёма света – считаем освещённым
    if (proj.x < -1.0f || proj.x > 1.0f ||
        proj.y < -1.0f || proj.y > 1.0f ||
        proj.z < 0.0f || proj.z > 1.0f)
    {
        return 1.0f;
    }

    // NDC [-1;1] -> UV [0;1]
    float2 uv = proj.xy * 0.5f + 0.5f;
    float depth = proj.z;

    // slope-scaled bias
    float ndotl = saturate(dot(N, Ld));
    float bias = max(0.0015f, 0.0035f * (1.0f - ndotl));

    // размер texel’а
    uint w, h;
    gShadowMap.GetDimensions(w, h);
    float2 texelSize = 1.0f / float2((float) w, (float) h);

    // PCF 5×5 вокруг текущего texel'а
    float shadow = 0.0f;
    const int radius = 2; // радиус ядра в texel'ах
    const int kernelSize = (radius * 2 + 1);
    const float invKernelArea = 1.0f / (kernelSize * kernelSize);

    [unroll]
    for (int x = -radius; x <= radius; ++x)
    {
        [unroll]
        for (int y = -radius; y <= radius; ++y)
        {
            float2 off = uv + float2(x, y) * texelSize;
            off = saturate(off);

            float mapDepth = gShadowMap.Sample(gShadowSam, off).r;

            // 1.0 – освещён, 0.0 – в тени
            shadow += (depth - bias > mapDepth) ? 0.0f : 1.0f;
        }
    }

    shadow *= invKernelArea;

    // не делаем тень абсолютно чёрной – минимум 20 % света
    return 0.2f + 0.8f * shadow;
}

// ---------------- Пиксельный шейдер основного прохода ----------------
float4 PSMain(VSOut i) : SV_TARGET
{
    // Маркеры источников (кружочки): теги [2; 5)
    if (i.tag >= 2.0f && i.tag < 5.0f)
    {
        float3 lightColor;

        if (i.tag < 2.5f)          // направленный
            lightColor = float3(1.0, 1.0, 0.2);
        else if (i.tag < 3.5f)     // прожектор 0
            lightColor = float3(1.0, 0.3, 0.3);
        else // прожектор 1
            lightColor = float3(0.3, 0.6, 1.0);

        float2 uvCircle = i.obj.xz;
        float r = length(uvCircle);
        float mask = step(r, 1.0);

        float3 bg = float3(0.2, 0.2, 0.2);
        float3 col = lerp(bg, lightColor, mask);

        return float4(col, 1.0);
    }

    float3 N = normalize(i.nrmW);
    float3 V = normalize(camPos - i.posW);

    bool isFloor = (i.tag > 0.5f && i.tag < 1.5f);
    bool isCone = (i.tag < 0.5f);
    bool isCube0 = (i.tag > 9.5f && i.tag < 10.5f);
    bool isCube1 = (i.tag > 10.5f && i.tag < 11.5f);
    bool isCube = (isCube0 || isCube1);

    float3 matAlbedo = i.matAlbedo;
    float3 matSpec = i.matSpec;
    float shininess = i.matShin;

    float3 albedo;

    // Текстурные кубы
    if (isCube0)
    {
        albedo = gDiffuseTex0.Sample(gTextureSam, i.uv).rgb;
    }
    else if (isCube1)
    {
        albedo = gDiffuseTex1.Sample(gTextureSam, i.uv).rgb;
    }
    // Пол с шахматным паттерном
    else if (isFloor)
    {
        float2 p = i.obj.xz * 10.0;
        float2 cell = floor(p);
        float check = fmod(abs(cell.x + cell.y), 2.0);

        float3 c0 = matAlbedo * 0.25;
        float3 c1 = matAlbedo;
        albedo = lerp(c0, c1, check);
    }
    // Коны с полосочками
    else
    {
        float3 base = matAlbedo;
        float3 stripeCol = matAlbedo * float3(0.10, 0.10, 2.50);

        float ang = atan2(i.obj.z, i.obj.x);
        float stripeWidth = 0.15;
        float m = smoothstep(1.0 - stripeWidth, 1.0, cos(ang));

        albedo = lerp(base, stripeCol, m);
    }

    // ambient не затеняем
    float3 color = ambientColor * albedo;

    // Направленный свет + тени
    {
        float3 Ld = normalize(-dirLightDir);
        float shadow = ComputeShadow(i.posW, N, Ld);

        // отключаем тени для граней кубов (self-shadow)
        if (isCube)
        {
            shadow = 1.0f;
            // если хочется чуть-чуть мягкого заглушения, вместо 1.0f можно:
            // shadow = saturate(0.7f + 0.3f * shadow);
        }

        float specScaleDir = isFloor ? 0.2f : 0.7f;

        color += BlinnPhong(
            N, V, Ld,
            dirLightColor,
            albedo, matSpec,
            shininess, specScaleDir) * shadow;
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

        float3 Lp = -L;
        float cosTheta = dot(sl.dir, Lp);

        float spotFactor = saturate(
            (cosTheta - sl.cosOuter) /
            (sl.cosInner - sl.cosOuter));
        spotFactor = pow(spotFactor, 4.0f);

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

// ---------------- Вершинный шейдер shadow-pass ----------------
struct VSShadowOut
{
    float4 pos : SV_Position;
};

VSShadowOut VSShadowMain(VSInput v, uint instId : SV_InstanceID)
{
    uint idx = gBaseInstance + instId;
    InstanceData inst = gInstances[idx];

    float4 wp = mul(float4(v.pos, 1.0f), inst.world);

    VSShadowOut o;
    o.pos = mul(wp, lightViewProj); // тот же lightViewProj, что и в CameraCB
    return o;
}
