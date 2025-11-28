#include "Shared.hlsli"

// Две текстуры и один сэмплер
Texture2D gDiffuseTex0 : register(t3); // куб 0
Texture2D gDiffuseTex1 : register(t4); // куб 1
Texture2D gShadowMap : register(t5); // НОВОЕ
SamplerState gTextureSam : register(s0);

// Вершинный шейдер
VSOut VSMain(VSInput v, uint instId : SV_InstanceID)
{
    uint idx = gBaseInstance + instId;
    InstanceData inst = gInstances[idx];

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

    o.uv = v.uv;

    // НОВОЕ: позиция в пространстве света
    o.lightPos = mul(wp, lightViewProj);

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

float ComputeShadow(float4 lightPos)
{
    // Проецируем в NDC
    float3 proj = lightPos.xyz / lightPos.w;

    // Если вне объёма проекции света – считаем освещённым
    if (proj.x < -1.0f || proj.x > 1.0f ||
        proj.y < -1.0f || proj.y > 1.0f ||
        proj.z < 0.0f || proj.z > 1.0f)
    {
        return 1.0f;
    }

    // NDC [-1,1] -> UV [0,1]
    float2 uv = proj.xy * 0.5f + 0.5f;

    // z уже в [0,1] для ortho LH
    float depth = proj.z;

    float mapDepth = gShadowMap.Sample(gTextureSam, uv).r;

    // простой bias
    const float bias = 0.001f;
    return (depth - bias > mapDepth) ? 0.1f : 1.0f; // 0.1 – «немножко» света остаётся
}

// Пиксельный шейдер
float4 PSMain(VSOut i) : SV_TARGET
{
    // Маркеры источников (кружочки): теги [2; 5)
    if (i.tag >= 2.0f && i.tag < 5.0f)
    {
        float3 lightColor;

        if (i.tag < 2.5f)          // направленный
        {
            lightColor = float3(1.0, 1.0, 0.2);
        }
        else if (i.tag < 3.5f)     // прожектор 0
        {
            lightColor = float3(1.0, 0.3, 0.3);
        }
        else // прожектор 1
        {
            lightColor = float3(0.3, 0.6, 1.0);
        }

        float2 uv = i.obj.xz;
        float r = length(uv);
        float mask = step(r, 1.0);

        float3 bg = float3(0.2, 0.2, 0.2);
        float3 col = lerp(bg, lightColor, mask);

        return float4(col, 1.0);
    }

    float3 N = normalize(i.nrmW);
    float3 V = normalize(camPos - i.posW);

    bool isFloor = (i.tag > 0.5f && i.tag < 1.5f);
    bool isCone = (i.tag < 0.5f);
    bool isCube0 = (i.tag > 9.5f && i.tag < 10.5f); // первый куб
    bool isCube1 = (i.tag > 10.5f && i.tag < 11.5f); // второй куб

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

    float3 color = ambientColor * albedo;

    // Направленный свет
    {
        float3 Ld = normalize(-dirLightDir);
        float specScaleDir = isFloor ? 0.2f : 0.7f;

        float shadow = ComputeShadow(i.lightPos);

        color += BlinnPhong(
        N, V, Ld,
        dirLightColor,
        albedo, matSpec,
        shininess, specScaleDir) * shadow;
    }

    // Точечные источники (если будут)
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
    o.pos = mul(wp, lightViewProj);
    return o;
}
