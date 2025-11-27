#pragma once
#include <vector>
#include <cstdint>
#include <cmath>
#include <numbers>

struct ConeVertex { float px, py, pz, nx, ny, nz; };

/// √енерирует треугольную сетку правого конуса высотой 1 и радиусом 1 с вертикальной осью Y.
/// ¬ход: N >= 3 Ч количество сегментов окружности.
/// ¬ыход: verts Ч вершины с позици€ми и нормал€ми; idx Ч индексы треугольников.
inline void BuildConeMesh(uint32_t N, std::vector<ConeVertex>& verts, std::vector<uint32_t>& idx)
{
    verts.clear();
    idx.clear();
    if (N < 3) return;

    const float twoPi = 2.0f * std::numbers::pi_v<float>;
    const float height = 1.0f;
    const float radius = 1.0f;

    auto pushTri = [&](const ConeVertex& a, const ConeVertex& b, const ConeVertex& c) {
        const uint32_t base = static_cast<uint32_t>(verts.size());
        verts.push_back(a);
        verts.push_back(b);
        verts.push_back(c);
        idx.push_back(base + 0);
        idx.push_back(base + 1);
        idx.push_back(base + 2);
        };

    // ------------------------
    // Ѕќ ќ¬јя ѕќ¬≈–’Ќќ—“№
    // ------------------------
    // нормаль боковой поверхности: направление (x, radius/height, z), нормализованное
    const float nySlope = radius / height;

    for (uint32_t i = 0; i < N; ++i) {
        const float a0 = (twoPi * static_cast<float>(i)) / static_cast<float>(N);
        const float a1 = (twoPi * static_cast<float>(i + 1)) / static_cast<float>(N);

        const float x0 = std::cos(a0);
        const float z0 = std::sin(a0);
        const float x1 = std::cos(a1);
        const float z1 = std::sin(a1);

        // нормаль в точке окружности 0
        {
            const float len0 = std::sqrt(x0 * x0 + nySlope * nySlope + z0 * z0);
            const float nx0 = x0 / len0;
            const float ny0 = nySlope / len0;
            const float nz0 = z0 / len0;

            const float len1 = std::sqrt(x1 * x1 + nySlope * nySlope + z1 * z1);
            const float nx1 = x1 / len1;
            const float ny1 = nySlope / len1;
            const float nz1 = z1 / len1;

            // нормаль у вершины конуса как среднее двух
            float ntx = nx0 + nx1;
            float nty = ny0 + ny1;
            float ntz = nz0 + nz1;
            const float nlen = std::sqrt(ntx * ntx + nty * nty + ntz * ntz);
            if (nlen > 0.0f) {
                ntx /= nlen; nty /= nlen; ntz /= nlen;
            }

            ConeVertex tip{ 0.0f, height, 0.0f, ntx, nty, ntz };
            ConeVertex v0{ x0, 0.0f, z0, nx0, ny0, nz0 };
            ConeVertex v1{ x1, 0.0f, z1, nx1, ny1, nz1 };

            // ѕор€док вершин (tip, v0, v1) даЄт внешнюю нормаль Ђнаружуї конуса
            pushTri(tip, v0, v1);
        }
    }

    // ------------------------
    // ƒЌќ (крышка снизу)
    // ------------------------
    // ÷ентр основани€, нормаль вверх (видимое сверху дно)
    const uint32_t centerIndex = static_cast<uint32_t>(verts.size());
    verts.push_back(ConeVertex{ 0.0f, 0.0f, 0.0f,
                                0.0f, 1.0f, 0.0f });

    for (uint32_t i = 0; i < N; ++i) {
        const float a0 = (twoPi * static_cast<float>(i)) / static_cast<float>(N);
        const float a1 = (twoPi * static_cast<float>(i + 1)) / static_cast<float>(N);

        ConeVertex v0{ std::cos(a0), 0.0f, std::sin(a0),
                       0.0f, 1.0f, 0.0f };
        ConeVertex v1{ std::cos(a1), 0.0f, std::sin(a1),
                       0.0f, 1.0f, 0.0f };

        const uint32_t base = static_cast<uint32_t>(verts.size());
        verts.push_back(v0);
        verts.push_back(v1);

        // ¬ажно: пор€док индексов даЄт нормаль +Y (фронт Ч сверху)
        idx.push_back(centerIndex);   // центр
        idx.push_back(base + 0);      // v0
        idx.push_back(base + 1);      // v1
    }
}
