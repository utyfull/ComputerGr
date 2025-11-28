#pragma once

#include <vector>
#include <cstdint>
#include <cmath>
#include <numbers>

struct ConeVertex
{
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
};

// √енерирует треугольную сетку правого конуса высотой 1 и радиусом 1 с осью вдоль Y.
// N >= 3 Ч количество сегментов окружности.
inline void BuildConeMesh(uint32_t N, std::vector<ConeVertex>& verts, std::vector<uint32_t>& idx)
{
    verts.clear();
    idx.clear();

    if (N < 3)
        return;

    const float twoPi = 2.0f * std::numbers::pi_v<float>;
    const float height = 1.0f;
    const float radius = 1.0f;

    auto pushTri = [&](const ConeVertex& a, const ConeVertex& b, const ConeVertex& c)
        {
            const uint32_t base = static_cast<uint32_t>(verts.size());

            verts.push_back(a);
            verts.push_back(b);
            verts.push_back(c);

            idx.push_back(base + 0);
            idx.push_back(base + 1);
            idx.push_back(base + 2);
        };

    const float nySlope = radius / height;

    for (uint32_t i = 0; i < N; ++i)
    {
        const float a0 = twoPi * static_cast<float>(i) / static_cast<float>(N);
        const float a1 = twoPi * static_cast<float>(i + 1U) / static_cast<float>(N);

        const float x0 = std::cos(a0);
        const float z0 = std::sin(a0);
        const float x1 = std::cos(a1);
        const float z1 = std::sin(a1);

        const float len0 = std::sqrt(x0 * x0 + nySlope * nySlope + z0 * z0);
        const float nx0 = x0 / len0;
        const float ny0 = nySlope / len0;
        const float nz0 = z0 / len0;

        const float len1 = std::sqrt(x1 * x1 + nySlope * nySlope + z1 * z1);
        const float nx1 = x1 / len1;
        const float ny1 = nySlope / len1;
        const float nz1 = z1 / len1;

        float ntx = nx0 + nx1;
        float nty = ny0 + ny1;
        float ntz = nz0 + nz1;

        const float nlen = std::sqrt(ntx * ntx + nty * nty + ntz * ntz);
        if (nlen > 0.0f)
        {
            ntx /= nlen;
            nty /= nlen;
            ntz /= nlen;
        }

        ConeVertex tip{ 0.0f, height, 0.0f,  ntx, ny0, ntz, 0.0f, 0.0f };
        ConeVertex v0{ x0,   0.0f,   z0,    nx0, ny0, nz0, 0.0f, 0.0f };
        ConeVertex v1{ x1,   0.0f,   z1,    nx1, ny1, nz1, 0.0f, 0.0f };

        pushTri(tip, v0, v1);
    }

    // ƒно
    const uint32_t centerIndex = static_cast<uint32_t>(verts.size());

    verts.push_back(ConeVertex
        {
            0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f,
            0.5f, 0.5f
        });

    for (uint32_t i = 0; i < N; ++i)
    {
        const float a0 = twoPi * static_cast<float>(i) / static_cast<float>(N);
        const float a1 = twoPi * static_cast<float>(i + 1U) / static_cast<float>(N);

        float u0 = 0.5f + 0.5f * std::cos(a0);
        float v0 = 0.5f - 0.5f * std::sin(a0);
        float u1 = 0.5f + 0.5f * std::cos(a1);
        float v1 = 0.5f - 0.5f * std::sin(a1);

        ConeVertex v0v
        {
            std::cos(a0), 0.0f, std::sin(a0),
            0.0f, 1.0f, 0.0f,
            u0, v0
        };

        ConeVertex v1v
        {
            std::cos(a1), 0.0f, std::sin(a1),
            0.0f, 1.0f, 0.0f,
            u1, v1
        };

        const uint32_t base = static_cast<uint32_t>(verts.size());

        verts.push_back(v0v);
        verts.push_back(v1v);

        idx.push_back(centerIndex);
        idx.push_back(base + 0);
        idx.push_back(base + 1);
    }
}
