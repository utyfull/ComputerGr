#ifndef SHARED_HLSLI
#define SHARED_HLSLI

// Входные данные вершинного шейдера
struct VSInput
{
    float3 pos : POSITION;
    float3 nrm : NORMAL;
    float2 uv : TEXCOORD0;
};

// Материал объекта
struct Material
{
    float3 albedo;
    float shininess;

    float3 specColor;
    float _pad;
};

// Данные инстанса
struct InstanceData
{
    float4x4 world;
    float4 tag;
    Material mat;
};

// Инстансы (structured buffer)
StructuredBuffer<InstanceData> gInstances : register(t0);

// Камера, ambient и направленный свет
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

// Base instance и количество источников
cbuffer ObjectCB : register(b1)
{
    uint gBaseInstance;
    uint gNumPointLights;
    uint gNumSpotLights;
    uint _padObj;
};

// Точечный источник
struct PointLight
{
    float3 pos;
    float attK; // 1 / (1 + attK * d^2)

    float3 color;
    float _pad;
};

// Прожектор
struct SpotLight
{
    float3 pos;
    float attK;

    float3 dir;
    float cosInner;

    float3 color;
    float cosOuter;
};

// Точечные (t1) и прожекторы (t2)
StructuredBuffer<PointLight> gPointLights : register(t1);
StructuredBuffer<SpotLight> gSpotLights : register(t2);

// Выход вершины
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

    float2 uv : TEXCOORD6;
};

#endif // SHARED_HLSLI
