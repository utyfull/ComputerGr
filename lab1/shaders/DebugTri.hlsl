struct VSOut
{
    float4 pos : SV_Position;
    float4 col : COLOR0;
};

VSOut VSMain(uint vid : SV_VertexID)
{
    float2 p[3] = { float2(-0.8, -0.8), float2(0.0, 0.8), float2(0.8, -0.8) };
    float3 c[3] = { float3(1, 0, 0), float3(0, 1, 0), float3(0, 0, 1) };
    VSOut o;
    o.pos = float4(p[vid], 0.0, 1.0);
    o.col = float4(c[vid], 1.0);
    return o;
}

float4 PSMain(VSOut i) : SV_Target
{
    return float4(0, 0, 0, 1);
}
