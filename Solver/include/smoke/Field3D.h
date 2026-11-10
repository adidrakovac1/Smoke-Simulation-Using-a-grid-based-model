#pragma once

#include "smoke/GridSize.h"

#include <cstddef>
#include <memory>
#include <utility>

namespace smoke {

/// One scalar field on the grid (e.g. density, temperature, one velocity component, pressure).
///
/// Memory layout (see docs/plan/ARCHITECTURE.md, section 3.2):
///   * indices i in [0, nx+1], j in [0, ny+1], k in [0, nz+1]; index 0 and n+1 are the ghost layer,
///     1..n are the interior cells (this follows Stam's code, so we can compare with the paper);
///   * i is the fastest index (neighbours in x are neighbours in memory) so SIMD can later
///     vectorise the innermost loop;
///   * the row length is padded up to a multiple of 8 floats (32 bytes), and the whole array is
///     64-byte aligned, so every row starts on an aligned address;
///   * one array per quantity ("structure of arrays"), not one struct per cell.
///
/// NOTE (M1): this is the minimal storage needed by the skeleton. M1 may refine it.
class Field3D {
public:
    static constexpr int         kPitchMultiple = 8;   // floats per padding unit
    static constexpr std::size_t kAlignment     = 64;  // bytes

    Field3D() = default;
    explicit Field3D(const GridSize& size) { resize(size); }

    /// Reallocates for a new size and fills everything with 0.
    void resize(const GridSize& size);
    void fill(float value);

    const GridSize& size() const { return size_; }

    /// Number of floats to skip to move one cell in j (row) and in k (plane).
    int strideJ() const { return strideJ_; }
    int strideK() const { return strideK_; }
    std::size_t totalFloats() const { return total_; }

    /// Pointer to cell (0,0,0), i.e. the ghost corner.
    float*       data()       { return data_.get(); }
    const float* data() const { return data_.get(); }

    std::size_t index(int i, int j, int k) const {
        return static_cast<std::size_t>(i) + static_cast<std::size_t>(strideJ_) * static_cast<std::size_t>(j) +
               static_cast<std::size_t>(strideK_) * static_cast<std::size_t>(k);
    }
    float&       at(int i, int j, int k)       { return data_[index(i, j, k)]; }
    const float& at(int i, int j, int k) const { return data_[index(i, j, k)]; }

    /// Exchanges the contents of two fields by swapping their pointers (no data is copied).
    /// Used for double buffering: advection and Jacobi read the "old" field and write the "new" one,
    /// and before each such stage the pair is swapped (docs/plan/ARCHITECTURE.md, section 5, rule 2).
    void swap(Field3D& other) noexcept {
        using std::swap;
        swap(size_, other.size_);
        swap(strideJ_, other.strideJ_);
        swap(strideK_, other.strideK_);
        swap(total_, other.total_);
        data_.swap(other.data_);
    }
    friend void swap(Field3D& a, Field3D& b) noexcept { a.swap(b); }

private:
    struct AlignedDeleter {
        void operator()(float* p) const noexcept;
    };

    GridSize    size_{0, 0, 0};
    int         strideJ_ = 0;
    int         strideK_ = 0;
    std::size_t total_   = 0;
    std::unique_ptr<float[], AlignedDeleter> data_;
};

} // namespace smoke
