#include "smoke/Field3D.h"

#include <algorithm>
#include <new>

namespace smoke {

void Field3D::AlignedDeleter::operator()(float* p) const noexcept {
    ::operator delete[](p, std::align_val_t{kAlignment});
}

void Field3D::resize(const GridSize& size) {
    size_ = size;
    if (!size.valid()) {
        data_.reset();
        strideJ_ = strideK_ = 0;
        total_ = 0;
        return;
    }

    // Row length including both ghost cells, rounded up so that every row starts on a 32-byte boundary.
    const int rowFloats = size.nx + 2;
    strideJ_ = ((rowFloats + kPitchMultiple - 1) / kPitchMultiple) * kPitchMultiple;
    strideK_ = strideJ_ * (size.ny + 2);
    total_   = static_cast<std::size_t>(strideK_) * static_cast<std::size_t>(size.nz + 2);

    auto* raw = static_cast<float*>(::operator new[](total_ * sizeof(float), std::align_val_t{kAlignment}));
    data_.reset(raw);
    fill(0.0f);
}

void Field3D::fill(float value) {
    if (data_) std::fill(data_.get(), data_.get() + total_, value);
}

} // namespace smoke
