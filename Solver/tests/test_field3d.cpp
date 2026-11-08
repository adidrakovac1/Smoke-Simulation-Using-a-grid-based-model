#include "check.h"
#include "smoke/Field3D.h"

#include <cstdint>

using smoke::Field3D;
using smoke::GridSize;

SMOKE_TEST(field3d_allocates_zeroed_and_aligned) {
    Field3D f(GridSize{64, 64, 64});
    CHECK(f.data() != nullptr);
    CHECK(reinterpret_cast<std::uintptr_t>(f.data()) % Field3D::kAlignment == 0);
    for (std::size_t i = 0; i < f.totalFloats(); ++i) {
        if (f.data()[i] != 0.0f) { CHECK(false); break; }
    }
}

SMOKE_TEST(field3d_row_pitch_is_multiple_of_8) {
    // 66 floats (64 + 2 ghost cells) must be padded to 72 so every row starts on a 32-byte boundary.
    Field3D f(GridSize{64, 64, 64});
    CHECK(f.strideJ() == 72);
    CHECK(f.strideJ() % Field3D::kPitchMultiple == 0);
    CHECK(f.strideK() == f.strideJ() * (64 + 2));
    CHECK(f.totalFloats() == static_cast<std::size_t>(f.strideK()) * (64 + 2));
}

SMOKE_TEST(field3d_non_cubic_grid) {
    Field3D f(GridSize{10, 20, 30});
    CHECK(f.size().nx == 10 && f.size().ny == 20 && f.size().nz == 30);
    CHECK(f.strideJ() == 16);  // 12 -> 16
    CHECK(f.strideK() == 16 * 22);
    CHECK(f.totalFloats() == static_cast<std::size_t>(16 * 22) * 32);
}

SMOKE_TEST(field3d_cells_are_distinct_including_ghost_layer) {
    Field3D f(GridSize{4, 5, 6});
    int value = 0;
    for (int k = 0; k <= 7; ++k)
        for (int j = 0; j <= 6; ++j)
            for (int i = 0; i <= 5; ++i) f.at(i, j, k) = static_cast<float>(++value);
    value = 0;
    bool same = true;
    for (int k = 0; k <= 7; ++k)
        for (int j = 0; j <= 6; ++j)
            for (int i = 0; i <= 5; ++i) same = same && (f.at(i, j, k) == static_cast<float>(++value));
    CHECK(same);
}

SMOKE_TEST(field3d_i_is_the_fastest_index) {
    Field3D f(GridSize{8, 8, 8});
    CHECK(f.index(1, 0, 0) == f.index(0, 0, 0) + 1);
    CHECK(f.index(0, 1, 0) == f.index(0, 0, 0) + static_cast<std::size_t>(f.strideJ()));
    CHECK(f.index(0, 0, 1) == f.index(0, 0, 0) + static_cast<std::size_t>(f.strideK()));
}

SMOKE_TEST(field3d_resize_reallocates_and_zeroes) {
    Field3D f(GridSize{16, 16, 16});
    f.fill(3.0f);
    f.resize(GridSize{32, 8, 8});
    CHECK(f.size().nx == 32);
    CHECK(f.at(1, 1, 1) == 0.0f);
    f.resize(GridSize{0, 0, 0});   // invalid size releases the memory
    CHECK(f.data() == nullptr);
}
