cbuffer CameraCB : register(b0)
{
    float4x4 viewProj;
};

cbuffer ObjectCB : register(b1)
{
    uint gBaseInstance;
};

struct InstanceData
{
    float4x4 world;
    float4 tag; // x: 0 = конус, 1 = пол
};

StructuredBuffer<InstanceData> gInstances : register(t0);

struct VSInput
{
    float3 pos : POSITION;
    float3 nrm : NORMAL;
};

struct VSOut
{
    float4 pos : SV_Position;
    float3 nrm : NORMAL;
    float3 obj : TEXCOORD0;
    float tag : TEXCOORD1;
};
