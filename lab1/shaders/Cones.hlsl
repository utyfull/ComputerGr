#include "Shared.hlsli"

/*
Вершинный шейдер.
Назначение: трансформирует позицию вершины из объектного пространства в клип-пространство,
преобразует нормаль в мировое пространство, пробрасывает объектную позицию для процедурного узора.
Входы: VSInput v, SV_InstanceID instId для выбора InstanceData.
Выход: VSOut с полями pos (SV_Position), nrm, obj.
*/
VSOut VSMain(VSInput v, uint instId : SV_InstanceID)
{
    InstanceData inst = gInstances[instId];

    const float4 wp = mul(float4(v.pos, 1.0), inst.world);

    VSOut o;
    o.pos = mul(wp, viewProj);
    o.nrm = mul((float3x3) inst.world, v.nrm);
    o.obj = v.pos;
    return o;
}

/*
Пиксельный шейдер.
Назначение: вычисляет ламбертово освещение с постоянной направленной подсветкой и
генерирует угловой полосатый узор по координате atan2(obj.z, obj.x) в объектном пространстве.
Параметры: базовый цвет и цвет полосы смешиваются через smoothstep с шириной полосы.
Выход: RGBA-цвет с альфой 1.
*/
float4 PSMain(VSOut i) : SV_TARGET
{
    const float3 N = normalize(i.nrm);
    const float3 L = normalize(float3(0.4, 0.8, 0.4));
    const float ndotl = saturate(dot(N, L));

    const float3 base = float3(0.93, 0.35, 0.15);

    const float ang = atan2(i.obj.z, i.obj.x);
    const float stripeWidth = 0.15;
    const float m = smoothstep(1.0 - stripeWidth, 1.0, cos(ang));

    const float3 stripeCol = float3(0.05, 0.05, 0.95);
    const float3 albedo = lerp(base, stripeCol, m);

    const float3 col = albedo * (0.2 + 0.8 * ndotl);
    return float4(col, 1.0);
}
