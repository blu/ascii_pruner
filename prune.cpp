// pruning of blanks from an ascii stream -- timing of candidate routines
#if __aarch64__
	#include <arm_neon.h>
#elif __SSSE3__
	#include <tmmintrin.h>
#elif __SSE2__
	#include <emmintrin.h>
#endif
#include <stdio.h>
#include <stdint.h>

uint8_t input[32] __attribute__ ((aligned(16))) = "alabalanica 1234";
uint8_t output[32] __attribute__ ((aligned(16)));
static const size_t misalign = 1; // simulate mis-alignment at write

// print utility
#if __aarch64__
void print_uint8x16(
	const uint8x16_t x,
	FILE* f = stderr) {

	fprintf(f, "{ ");
#define LANE(lane) \
	const uint8_t s##lane = vgetq_lane_u8(x, lane); \
	fprintf(f, "%.3hhu, ", s##lane);

	LANE( 0)
	LANE( 1)
	LANE( 2)
	LANE( 3)
	LANE( 4)
	LANE( 5)
	LANE( 6)
	LANE( 7)

	LANE( 8)
	LANE( 9)
	LANE(10)
	LANE(11)
	LANE(12)
	LANE(13)
	LANE(14)

#undef LANE
	const uint8_t last = vgetq_lane_u8(x, 15);
	fprintf(f, "%.3hhu }", last);
}

#elif __SSE2__
void print_uint8x16(
	const __m128i x,
	FILE* f = stderr) {

	fprintf(f, "{ ");

	uint64_t head = _mm_cvtsi128_si64(x);
	for (size_t j = 0; j < sizeof(head) / sizeof(uint8_t); ++j) {
		fprintf(f, "%.3hhu, ", uint8_t(head));
		head >>= sizeof(uint8_t) * 8;
	}

	uint64_t tail = _mm_cvtsi128_si64(_mm_shuffle_epi32(x, 0xee));
	for (size_t j = 0; j < sizeof(tail) / sizeof(uint8_t) - 1; ++j) {
		fprintf(f, "%.3hhu, ", uint8_t(tail));
		tail >>= sizeof(uint8_t) * 8;
	}
	fprintf(f, "%.3hhu }", uint8_t(tail));
}

#endif
// fully-scalar version; good performance on both amd64 and arm64 above-entry-level parts;
// particularly on cortex-a72 this does an IPC of 2.94 which is excellent! ryzen also
// does an IPC above 4, which is remarkable
inline void testee00() {
	size_t i = 0, pos = 0;
	while (i < 16) {
		const char c = input[i++];
		output[pos] = c;
		pos += (c > 32 ? 1 : 0);
	}
}

#if __aarch64__
// ASIMD2 version; in-vector string compaction via permute ops (tbl) using prefix-sum indices
inline void testee01() {
	uint8x16_t const vinput = vld1q_u8(input);
	uint8x16_t prfsum = vcleq_u8(vinput, vdupq_n_u8(' '));

	// before computing the actual prefix sum: count_of_blanks := vaddvq_u8(prfsum)
	prfsum = vaddq_u8(prfsum, vextq_u8(vdupq_n_u8(0), prfsum, 16 - 1));
	prfsum = vaddq_u8(prfsum, vextq_u8(vdupq_n_u8(0), prfsum, 16 - 2));
	prfsum = vaddq_u8(prfsum, vextq_u8(vdupq_n_u8(0), prfsum, 16 - 4));
	prfsum = vaddq_u8(prfsum, vextq_u8(vdupq_n_u8(0), prfsum, 16 - 8));

	uint8x16_t const index = vsubq_u8((uint8x16_t) { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }, prfsum);

	// couldn't find an intrinsic for 16-byte tbl op
	uint8x16_t res;
	asm volatile ("tbl %0.16b, {%1.16b}, %2.16b"
		: "=w" (res) : "w" (vinput), "w" (index) : );

	vst1q_u8(output + misalign, res);
	// omitted from this test: output ptr needs to be advanced by count of non-blanks
}

inline void testee02() {
	uint8x16_t const vinput0 = vld1q_u8(input);
	uint8x16_t const vinput1 = vld1q_u8(input + sizeof(uint8x16_t));
	uint8x16_t prfsum0 = vcleq_u8(vinput0, vdupq_n_u8(' '));
	uint8x16_t prfsum1 = vcleq_u8(vinput1, vdupq_n_u8(' '));

	// before computing the actual prefix sum: count_of_blanks := vaddvq_u8(prfsum)
	const int8_t blen0 = vaddvq_u8(prfsum0);
	const int8_t blen1 = vaddvq_u8(prfsum1);

	prfsum0 = vaddq_u8(prfsum0, vextq_u8(vdupq_n_u8(0), prfsum0, 16 - 1));
	prfsum1 = vaddq_u8(prfsum1, vextq_u8(vdupq_n_u8(0), prfsum1, 16 - 1));
	prfsum0 = vaddq_u8(prfsum0, vextq_u8(vdupq_n_u8(0), prfsum0, 16 - 2));
	prfsum1 = vaddq_u8(prfsum1, vextq_u8(vdupq_n_u8(0), prfsum1, 16 - 2));
	prfsum0 = vaddq_u8(prfsum0, vextq_u8(vdupq_n_u8(0), prfsum0, 16 - 4));
	prfsum1 = vaddq_u8(prfsum1, vextq_u8(vdupq_n_u8(0), prfsum1, 16 - 4));
	prfsum0 = vaddq_u8(prfsum0, vextq_u8(vdupq_n_u8(0), prfsum0, 16 - 8));
	prfsum1 = vaddq_u8(prfsum1, vextq_u8(vdupq_n_u8(0), prfsum1, 16 - 8));

	uint8x16_t const index0 = vsubq_u8((uint8x16_t) { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }, prfsum0);
	uint8x16_t const index1 = vsubq_u8((uint8x16_t) { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }, prfsum1);

	// couldn't find an intrinsic for 16-byte tbl op
	uint8x16_t res0, res1;
	asm volatile (
		"tbl %0.16b, {%2.16b}, %3.16b\n\t"
		"tbl %1.16b, {%4.16b}, %5.16b"
		: "=w" (res0), "=w" (res1)
		: "w" (vinput0), "w" (index0),
		  "w" (vinput1), "w" (index1) : );

	vst1q_u8(output + misalign, res0);
	vst1q_u8(output + misalign + sizeof(uint8x16_t) + blen0, res1);
	// omitted from this test: output ptr needs to be advanced by count of non-blanks
}

#elif __SSSE3__
// SSSE3 version; in-vector string compaction via permute ops (pshufb) using prefix-sum indices
inline void testee01() {
	__m128i const vinput = _mm_load_si128(reinterpret_cast< const __m128i* >(input));
	__m128i prfsum = _mm_cmplt_epi8(vinput, _mm_set1_epi8(' ' + 1));

	// before computing the actual prefix sum: count_of_blanks := _mm_movemask_epi8(prfsum)
	prfsum = _mm_add_epi8(prfsum, _mm_slli_si128(prfsum, 1));
	prfsum = _mm_add_epi8(prfsum, _mm_slli_si128(prfsum, 2));
	prfsum = _mm_add_epi8(prfsum, _mm_slli_si128(prfsum, 4));
	prfsum = _mm_add_epi8(prfsum, _mm_slli_si128(prfsum, 8));

	__m128i const index = _mm_sub_epi8(_mm_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15), prfsum);
	__m128i const res = _mm_shuffle_epi8(vinput, index);

	_mm_storeu_si128(reinterpret_cast< __m128i* >(output + misalign), res);
	// omitted from this test: output ptr needs to be advanced by count of non-blanks
}

#endif
int main(int, char**) {
	const size_t rep = size_t(5e7);

	for (size_t i = 0; i < rep; ++i) {
		// one of testees
		testee00();

		// iteration obfuscator
		asm volatile ("" : : : "memory");
	}
	fprintf(stderr, "%.32s\n", output + misalign);
	return 0;
}

