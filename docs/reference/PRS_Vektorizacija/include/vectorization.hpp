#pragma once
#include <cstddef>



namespace vectorization
{
	using Saxpy = void (*)(const float* x, float* y, std::size_t n, float a);
	using Dot = float (*)(const float* x, const float* y, std::size_t n);
	using Threshold = void (*)(const float* x, float* output, std::size_t n, float threshold, float scale);
	using Escape = void (*)(const float* cx, const float* cy, float* iterations, std::size_t n, int max_iterations);
	using CopyFn = void (*)(const float* src, float* out, std::size_t n, float scale);

	void saxpy_scalar(const float*, float*, std::size_t, float);
	void saxpy_auto(const float*, float*, std::size_t, float);
	void saxpy_omp(const float*, float*, std::size_t, float);
	void saxpy_avx2(const float*, float*, std::size_t, float);
	void saxpy_avx2_aligned(const float*, float*, std::size_t, float);


	float dot_scalar(const float*, const float*, std::size_t);
	float dot_auto(const float*, const float*, std::size_t);
	float dot_omp(const float*, const float*, std::size_t);
	float dot_avx2(const float*, const float*, std::size_t);


	void threshold_scalar(const float*, float*, std::size_t, float, float);
	void threshold_auto(const float*, float*, std::size_t, float, float);
	void threshold_omp(const float*, float*, std::size_t, float, float);
	void threshold_avx2(const float*, float*, std::size_t, float, float);


	void escape_scalar(const float*, const float*, float*, std::size_t, int);
	void escape_auto(const float*, const float*, float*, std::size_t, int);


	void fn_may_alias(const float* a, const float* b, float* out, std::size_t n);
	void fn_no_alias(const float* a, const float* b, float* out, std::size_t n);

	void prefix_scan(const unsigned* input, unsigned* output, std::size_t n);


}
