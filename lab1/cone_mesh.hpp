#pragma once
#include <vector>
#include <cstdint>
#include <cmath>
#include <numbers>

struct ConeVertex { float px, py, pz, nx, ny, nz; };

/// √енерирует треугольную сетку правого конуса высотой 1 и радиусом 1 с вертикальной осью Y.
/// ¬ход: N >= 3 Ч количество сегментов окружности.
/// ¬ыход: verts Ч вершины с позици€ми и нормал€ми; idx Ч индексы треугольников в пор€дке рисовани€.
inline void BuildConeMesh(uint32_t N, std::vector<ConeVertex>& verts, std::vector<uint32_t>& idx) {
    verts.clear();
    idx.clear();

    const uint32_t sideTris = N;
    const uint32_t baseTris = N;
    const uint32_t vertsPerSideTri = 3;
    const uint32_t vertsPerBaseTri = 2;
    verts.reserve(sideTris * vertsPerSideTri + 1 + baseTris * vertsPerBaseTri);
    idx.reserve((sideTris + baseTris) * 3);

    const float twoPi = 2.0f * std::numbers::pi_v<float>;

    auto pushTri = [&](const ConeVertex& a, const ConeVertex& b, const ConeVertex& c) {
        const uint32_t base = static_cast<uint32_t>(verts.size());
        verts.push_back(a);
        verts.push_back(b);
        verts.push_back(c);
        idx.push_back(base + 0);
        idx.push_back(base + 1);
        idx.push_back(base + 2);
        };

    for (uint32_t i = 0; i < N; ++i) {
        const float a0 = (twoPi * static_cast<float>(i)) / static_cast<float>(N);
        const float a1 = (twoPi * static_cast<float>(i + 1)) / static_cast<float>(N);
        const float x0 = std::cos(a0), z0 = std::sin(a0);
        const float x1 = std::cos(a1), z1 = std::sin(a1);
        const ConeVertex tip{ 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f };
        const ConeVertex v0{ x0, 0.0f, z0, x0, 0.5f, z0 };
        const ConeVertex v1{ x1, 0.0f, z1, x1, 0.5f, z1 };
        pushTri(tip, v0, v1);
    }

    const uint32_t center = static_cast<uint32_t>(verts.size());
    verts.push_back(ConeVertex{ 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f });

    for (uint32_t i = 0; i < N; ++i) {
        const float a0 = (twoPi * static_cast<float>(i)) / static_cast<float>(N);
        const float a1 = (twoPi * static_cast<float>(i + 1)) / static_cast<float>(N);
        const ConeVertex v0{ std::cos(a0), 0.0f, std::sin(a0), 0.0f, -1.0f, 0.0f };
        const ConeVertex v1{ std::cos(a1), 0.0f, std::sin(a1), 0.0f, -1.0f, 0.0f };
        const uint32_t b = static_cast<uint32_t>(verts.size());
        verts.push_back(v0);
        verts.push_back(v1);
        idx.push_back(center);
        idx.push_back(b + 1);
        idx.push_back(b + 0);
    }
}
