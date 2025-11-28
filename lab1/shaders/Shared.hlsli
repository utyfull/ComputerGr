#ifndef SHARED_HLSLI
#define SHARED_HLSLI

struct VSInput
{
    float3 pos : POSITION;
    float3 nrm : NORMAL;
};

struct Material
{
    float3 albedo;
    float shininess;

    float3 specColor;
    float _pad;
};

struct InstanceData
{
    float4x4 world;
    float4 tag;
    Material mat;
};

// »нстансы Ч уже StructuredBuffer
StructuredBuffer<InstanceData> gInstances : register(t0);

//  амера + ambient + направленный свет (cbuffer b0)
cbuffer CameraCB : register(b0)
{
    float4x4 viewProj;

    float3 camPos;
    float _pad0;

    float3 ambientColor;
    float _pad1;

    float3 dirLightDir;
    float _pad2;

    float3 dirLightColor;
    float _pad3;
};

// ƒл€ baseInstance + количества источников: b1 (root constants)
cbuffer ObjectCB : register(b1)
{
    uint gBaseInstance;
    uint gNumPointLights;
    uint gNumSpotLights;
    uint _padObj; // до 16 байт
};

// ----------------------------
// Shader-storage буферы огней
// ----------------------------

struct PointLight
{
    float3 pos;
    float attK; // 1 / (1 + attK * d^2)
    float3 color;
    float _pad;
};

struct SpotLight
{
    float3 pos;
    float attK;
    float3 dir;
    float cosInner;
    float3 color;
    float cosOuter;
};

// t1 Ч точечные, t2 Ч прожекторы
StructuredBuffer<PointLight> gPointLights : register(t1);
StructuredBuffer<SpotLight> gSpotLights : register(t2);

// ¬ыход VS
struct VSOut
{
    float4 pos : SV_Position;
    float3 nrmW : NORMAL;
    float3 posW : TEXCOORD0;
    float3 obj : TEXCOORD1;
    float tag : TEXCOORD2;

    float3 matAlbedo : TEXCOORD3;
    float3 matSpec : TEXCOORD4;
    float matShin : TEXCOORD5;
};

#endif // SHARED_HLSLI
