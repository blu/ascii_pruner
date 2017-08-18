// latency accumulator -- a chain of ops with data dependency between each two ops;
// to break the dependency set COISSUE

#if __aarch64__ == 0 && __SSSE3__ == 0
	#error wrong target arch
#endif

#include <stddef.h>

int main(int, char**) {
	size_t const rep = size_t(5e8);

	for (size_t i = 0; i < rep; ++i) {

#if __aarch64__
#if COISSUE
		asm volatile (
			"tbl v17.16b, {v1.16b},  v0.16b\n\t"
			"tbl v18.16b, {v2.16b},  v0.16b\n\t"
			"tbl v19.16b, {v3.16b},  v0.16b\n\t"
			"tbl v20.16b, {v4.16b},  v0.16b\n\t"
			"tbl v21.16b, {v5.16b},  v0.16b\n\t"
			"tbl v22.16b, {v6.16b},  v0.16b\n\t"
			"tbl v23.16b, {v7.16b},  v0.16b\n\t"
			"tbl v24.16b, {v8.16b},  v0.16b\n\t"
			"tbl v25.16b, {v9.16b},  v0.16b\n\t"
			"tbl v26.16b, {v10.16b}, v0.16b\n\t"
			"tbl v27.16b, {v11.16b}, v0.16b\n\t"
			"tbl v28.16b, {v12.16b}, v0.16b\n\t"
			"tbl v29.16b, {v13.16b}, v0.16b\n\t"
			"tbl v30.16b, {v14.16b}, v0.16b\n\t"
			"tbl v31.16b, {v15.16b}, v0.16b\n\t"
			"tbl v16.16b, {v16.16b}, v0.16b"
			: : : "memory");

#else
		asm volatile (
			"tbl v2.16b,  {v1.16b},  v0.16b\n\t"
			"tbl v3.16b,  {v2.16b},  v0.16b\n\t"
			"tbl v4.16b,  {v3.16b},  v0.16b\n\t"
			"tbl v5.16b,  {v4.16b},  v0.16b\n\t"
			"tbl v6.16b,  {v5.16b},  v0.16b\n\t"
			"tbl v7.16b,  {v6.16b},  v0.16b\n\t"
			"tbl v8.16b,  {v7.16b},  v0.16b\n\t"
			"tbl v9.16b,  {v8.16b},  v0.16b\n\t"
			"tbl v10.16b, {v9.16b},  v0.16b\n\t"
			"tbl v11.16b, {v10.16b}, v0.16b\n\t"
			"tbl v12.16b, {v11.16b}, v0.16b\n\t"
			"tbl v13.16b, {v12.16b}, v0.16b\n\t"
			"tbl v14.16b, {v13.16b}, v0.16b\n\t"
			"tbl v15.16b, {v14.16b}, v0.16b\n\t"
			"tbl v16.16b, {v15.16b}, v0.16b\n\t"
			"tbl v17.16b, {v16.16b}, v0.16b"
			: : : "memory");

#endif
#elif __SSSE3__
#if COISSUE
		asm volatile (
			"pshufb %%xmm0,  %%xmm1\n\t"
			"pshufb %%xmm0,  %%xmm2\n\t"
			"pshufb %%xmm0,  %%xmm3\n\t"
			"pshufb %%xmm0,  %%xmm4\n\t"
			"pshufb %%xmm0,  %%xmm5\n\t"
			"pshufb %%xmm0,  %%xmm6\n\t"
			"pshufb %%xmm0,  %%xmm7\n\t"
			"pshufb %%xmm0,  %%xmm8\n\t"
			"pshufb %%xmm0,  %%xmm9\n\t"
			"pshufb %%xmm0, %%xmm10\n\t"
			"pshufb %%xmm0, %%xmm11\n\t"
			"pshufb %%xmm0, %%xmm12\n\t"
			"pshufb %%xmm0, %%xmm13\n\t"
			"pshufb %%xmm0, %%xmm14\n\t"
			"pshufb %%xmm0, %%xmm15\n\t"
			"pshufb %%xmm0, %%xmm0"
			: : : "memory");

#else
		asm volatile (
			"pshufb %%xmm0,  %%xmm1\n\t"
			"pshufb %%xmm1,  %%xmm2\n\t"
			"pshufb %%xmm2,  %%xmm3\n\t"
			"pshufb %%xmm3,  %%xmm4\n\t"
			"pshufb %%xmm4,  %%xmm5\n\t"
			"pshufb %%xmm5,  %%xmm6\n\t"
			"pshufb %%xmm6,  %%xmm7\n\t"
			"pshufb %%xmm7,  %%xmm8\n\t"
			"pshufb %%xmm8,  %%xmm9\n\t"
			"pshufb %%xmm9,  %%xmm10\n\t"
			"pshufb %%xmm10, %%xmm11\n\t"
			"pshufb %%xmm11, %%xmm12\n\t"
			"pshufb %%xmm12, %%xmm13\n\t"
			"pshufb %%xmm13, %%xmm14\n\t"
			"pshufb %%xmm14, %%xmm15\n\t"
			"pshufb %%xmm15, %%xmm0"
			: : : "memory");

#endif
#endif
    }

    return 0;
}
