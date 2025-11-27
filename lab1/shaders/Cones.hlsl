// Cones.hlsl
#include "Shared.hlsli"

/*
Вершинный шейдер.
Назначение: трансформирует позицию вершины из объектного пространства в клип-пространство,
преобразует нормаль в мировое пространство, пробрасывает объектную позицию для процедурного узора
и tag инстанса (0 = конус, 1 = пол).
Входы: VSInput v, SV_InstanceID instId для выбора InstanceData.
Выход: VSOut с полями pos (SV_Position), nrm, obj, tag.
*/
VSOut VSMain(VSInput v, uint instId : SV_InstanceID)
{
    uint idx = gBaseInstance + instId;
    InstanceData inst = gInstances[idx];

    float4 wp = mul(float4(v.pos, 1.0), inst.world);

    VSOut o;
    o.pos = mul(wp, viewProj);
    o.nrm = mul((float3x3) inst.world, v.nrm);
    o.obj = v.pos;
    o.tag = inst.tag.x;
    return o;
}

/*
Пиксельный шейдер.
Если tag > 0.5 — рисуем пол как черно-белую шахматную доску в плоскости XZ.
Иначе (tag <= 0.5) — старый полосатый узор для конусов.
*/
float4 PSMain(VSOut i) : SV_TARGET
{
    // tag > 0.5  → пол (шахматка)
    if (i.tag > 0.5f)
    {
    // мелкие клетки
        float2 p = i.obj.xz * 10.0;
        float2 cell = floor(p);

    // сумма по модулю, чтобы не было отрицательных значений
        float checker = fmod(abs(cell.x + cell.y), 2.0);

        float3 c0 = float3(0.15, 0.15, 0.15); // «чёрная»
        float3 c1 = float3(0.90, 0.90, 0.90); // «белая»
        float3 col = lerp(c0, c1, checker); // checker = 0 или 1

        return float4(col, 1.0);
    }

    // ===== Конусы: только узор, БЕЗ освещения =====
    const float3 base = float3(0.93, 0.35, 0.15); // оранжевый

    const float ang = atan2(i.obj.z, i.obj.x);
    const float stripeWidth = 0.15;
    const float m = smoothstep(1.0 - stripeWidth, 1.0, cos(ang));

    const float3 stripeCol = float3(0.05, 0.05, 0.95); // синий
    const float3 col = lerp(base, stripeCol, m); // просто смешиваем цвета

    return float4(col, 1.0);
}