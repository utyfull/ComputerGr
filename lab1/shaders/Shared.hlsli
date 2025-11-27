// Вход вершины. Хранит позицию и нормаль в объектном пространстве.
struct VSInput
{
    float3 pos : POSITION;
    float3 nrm : NORMAL;
};

// Данные экземпляра. Хранит мировую матрицу для инстанс-рендеринга.
struct InstanceData
{
    float4x4 world;
};

// Камерный cbuffer. Содержит матрицу вида и проекции.
cbuffer CameraCB : register(b0)
{
    float4x4 viewProj;
};

// Буфер инстансов. Каждый экземпляр имеет свою мировую матрицу.
StructuredBuffer<InstanceData> gInstances : register(t0);

// Выход вершины. Передает позицию в клип-пространстве, нормаль
// в мировом пространстве и объектные координаты для процедурного паттерна.
struct VSOut
{
    float4 pos : SV_POSITION;
    float3 nrm : NORMAL;
    float3 obj : TEXCOORD0;
};
