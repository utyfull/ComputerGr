// Shared.hlsli

// b0: камера + все источники света
cbuffer CameraCB : register(b0)
{
    float4x4 viewProj;       // 64

    float3   camPos;         // позици€ камеры в мире
    float    _pad0;          // 16

    float3   ambientColor;   // рассе€нный свет
    float    _pad1;          // 16

    // направленный свет (направление »« источника в мир)
    float3   dirLightDir;
    float    _pad2;          // 16
    float3   dirLightColor;
    float    _pad3;          // 16

    // прожекторы (2 штуки)
    // spotPosRange[i].xyz Ч позици€ в мире, w Ч коэффициент дл€ затухани€
    float4   spotPosRange[2];

    // spotDirInner[i].xyz Ч нормализованное направление (из источника в сцену),
    // w Ч cos(innerAngle)
    float4   spotDirInner[2];

    // spotColorOuter[i].xyz Ч цвет/интенсивность,
    // w Ч cos(outerAngle)
    float4   spotColorOuter[2];
};

// b1: baseInstance
cbuffer ObjectCB : register(b1)
{
    uint gBaseInstance;
};

struct InstanceData
{
    float4x4 world;
    float4   tag;   // x: 0 = конус, 1 = пол
};

StructuredBuffer<InstanceData> gInstances : register(t0);

struct VSInput
{
    float3 pos : POSITION;
    float3 nrm : NORMAL;
};

struct VSOut
{
    float4 pos   : SV_Position;
    float3 nrmW  : NORMAL;     // нормаль в мировом
    float3 posW  : TEXCOORD0;  // позици€ в мировом
    float3 obj   : TEXCOORD1;  // объектные координаты (дл€ узоров)
    float  tag   : TEXCOORD2;  // 0 Ч конус, 1 Ч пол
};