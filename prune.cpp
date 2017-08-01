// pruning of blanks from an ascii stream -- timing of candidate routines
#if __aarch64__
	#include <arm_neon.h>
#elif __SSSE3__
	#include <tmmintrin.h>
#elif __SSE2__
	#include <emmintrin.h>
#endif
#if __POPCNT__
	#include <popcntintrin.h>
#endif
#include <stdio.h>
#include <stdint.h>

uint8_t input[32] __attribute__ ((aligned(16))) = " 1  2  3    45  ";
uint8_t output[32] __attribute__ ((aligned(16)));
static const size_t misalign = 1; // simulate mis-alignment at write

// print utility
#if __aarch64__
void print_uint8x16(
	uint8x16_t const x,
	bool const addNewLine,
	FILE* const f = stderr) {

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

	if (addNewLine)
		fprintf(f, "%.3hhu }\n", last);
	else
		fprintf(f, "%.3hhu }", last);
}

#elif __SSE2__
void print_uint8x16(
	__m128i const x,
	bool const addNewLine,
	FILE* const f = stderr) {

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

	if (addNewLine)
		fprintf(f, "%.3hhu }\n", uint8_t(tail));
	else
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
// sole pruner - ASIMD2 version; filter single blank per every N input chars, N = vector size
inline void testee01() {
	uint8x16_t const vinput = vld1q_u8(input);
	uint8x16_t prfsum = vcleq_u8(vinput, vdupq_n_u8(' '));

	// before computing the actual prefix sum: count_of_blanks := vaddvq_u8(prfsum)
	prfsum = vaddq_u8(prfsum, vextq_u8(vdupq_n_u8(0), prfsum, 16 - 1));
	prfsum = vaddq_u8(prfsum, vextq_u8(vdupq_n_u8(0), prfsum, 16 - 2));
	prfsum = vaddq_u8(prfsum, vextq_u8(vdupq_n_u8(0), prfsum, 16 - 4));
	prfsum = vaddq_u8(prfsum, vextq_u8(vdupq_n_u8(0), prfsum, 16 - 8));

	uint8x16_t const index = vsubq_u8((uint8x16_t) { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }, prfsum);

#if QUIRK_001_MISSCHEDULED_TBL
	// clang-3.7 produces worse scheduling when it encounters tbl -- put that in a black box
	uint8x16_t res;
	asm volatile ("tbl %0.16b, {%1.16b}, %2.16b"
		: "=w" (res) : "w" (vinput), "w" (index) : );

#else
	uint8x16_t const res = vqtbl1q_u8(vinput, index);

#endif
	vst1q_u8(output + misalign, res);
	// omitted from this test: output ptr needs to be advanced by count of non-blanks
}

// sole pruner - ASIMD2 version, 32-batch; filter single blank per every N input chars, N = vector size
inline void testee02() {
	uint8x16_t const vinput0 = vld1q_u8(input);
	uint8x16_t const vinput1 = vld1q_u8(input + sizeof(uint8x16_t));
	uint8x16_t prfsum0 = vcleq_u8(vinput0, vdupq_n_u8(' '));
	uint8x16_t prfsum1 = vcleq_u8(vinput1, vdupq_n_u8(' '));

	// before computing the actual prefix sum: count_of_blanks := vaddvq_u8(prfsum)
	int8_t const blen0 = vaddvq_u8(prfsum0);
	int8_t const blen1 = vaddvq_u8(prfsum1);

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

#if QUIRK_001_MISSCHEDULED_TBL
	// clang-3.7 produces worse scheduling when it encounters tbl -- put that in a black box
	uint8x16_t res0, res1;
	asm volatile (
		"tbl %0.16b, {%2.16b}, %3.16b\n\t"
		"tbl %1.16b, {%4.16b}, %5.16b"
		: "=w" (res0), "=w" (res1)
		: "w" (vinput0), "w" (index0),
		  "w" (vinput1), "w" (index1) : );

#else
	uint8x16_t const res0 = vqtbl1q_u8(vinput0, index0);
	uint8x16_t const res1 = vqtbl1q_u8(vinput1, index1);

#endif
	vst1q_u8(output + misalign, res0);
	vst1q_u8(output + misalign + sizeof(uint8x16_t) + blen0, res1);
	// omitted from this test: output ptr needs to be advanced by count of non-blanks
}

#elif __SSSE3__
// sole pruner - SSSE3 version; filter single blank per every N input chars, N = vector size
inline void testee01() {
	__m128i const vinput = _mm_load_si128(reinterpret_cast< const __m128i* >(input));
	__m128i prfsum = _mm_cmplt_epi8(vinput, _mm_set1_epi8(' ' + 1));

	// before computing the actual prefix sum: count_of_blanks := _mm_popcnt_u32(_mm_movemask_epi8(prfsum))
	prfsum = _mm_add_epi8(prfsum, _mm_slli_si128(prfsum, 1));
	prfsum = _mm_add_epi8(prfsum, _mm_slli_si128(prfsum, 2));
	prfsum = _mm_add_epi8(prfsum, _mm_slli_si128(prfsum, 4));
	prfsum = _mm_add_epi8(prfsum, _mm_slli_si128(prfsum, 8));

	__m128i const index = _mm_sub_epi8(_mm_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15), prfsum);
	__m128i const res = _mm_shuffle_epi8(vinput, index);

	_mm_storeu_si128(reinterpret_cast< __m128i* >(output + misalign), res);
	// omitted from this test: output ptr needs to be advanced by count of non-blanks
}

// sole pruner - SSSE3 version, 32-batch; filter single blank per every N input chars, N = vector size
inline void testee02() {
	__m128i const vinput0 = _mm_load_si128(reinterpret_cast< const __m128i* >(input));
	__m128i const vinput1 = _mm_load_si128(reinterpret_cast< const __m128i* >(input) + 1);
	__m128i prfsum0 = _mm_cmplt_epi8(vinput0, _mm_set1_epi8(' ' + 1));
	__m128i prfsum1 = _mm_cmplt_epi8(vinput1, _mm_set1_epi8(' ' + 1));

	// before computing the actual prefix sum: count_of_blanks := _mm_popcnt_u32(_mm_movemask_epi8(prfsum))
	uint32_t const blen0 = _mm_popcnt_u32(_mm_movemask_epi8(prfsum0));
	uint32_t const blen1 = _mm_popcnt_u32(_mm_movemask_epi8(prfsum1));

	prfsum0 = _mm_add_epi8(prfsum0, _mm_slli_si128(prfsum0, 1));
	prfsum1 = _mm_add_epi8(prfsum1, _mm_slli_si128(prfsum1, 1));
	prfsum0 = _mm_add_epi8(prfsum0, _mm_slli_si128(prfsum0, 2));
	prfsum1 = _mm_add_epi8(prfsum1, _mm_slli_si128(prfsum1, 2));
	prfsum0 = _mm_add_epi8(prfsum0, _mm_slli_si128(prfsum0, 4));
	prfsum1 = _mm_add_epi8(prfsum1, _mm_slli_si128(prfsum1, 4));
	prfsum0 = _mm_add_epi8(prfsum0, _mm_slli_si128(prfsum0, 8));
	prfsum1 = _mm_add_epi8(prfsum1, _mm_slli_si128(prfsum1, 8));

	__m128i const index0 = _mm_sub_epi8(_mm_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15), prfsum0);
	__m128i const index1 = _mm_sub_epi8(_mm_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15), prfsum1);
	__m128i const res0 = _mm_shuffle_epi8(vinput0, index0);
	__m128i const res1 = _mm_shuffle_epi8(vinput1, index1);

	_mm_storeu_si128(reinterpret_cast< __m128i* >(output + misalign), res0);
	_mm_storeu_si128(reinterpret_cast< __m128i* >(output + misalign + sizeof(__m128i) - blen0), res1);
	// omitted from this test: output ptr needs to be advanced by count of non-blanks
}

#endif
#if __SSE2__
// pruner semi -- replace blanks with the next non-blank
inline void testee03() {
	__m128i const vin = _mm_load_si128(reinterpret_cast< const __m128i* >(input));

	// discover non-blanks
	__m128i const pos = _mm_cmpgt_epi8(vin, _mm_set1_epi8(' '));

	// mark blanks as ones
	__m128i const spc = _mm_sub_epi8(pos, _mm_set1_epi8(-1));

	// prefix-sum the blanks, right to left
	__m128i prfsum = spc;
	prfsum = _mm_add_epi8(prfsum, _mm_srli_si128(prfsum, 1));
	prfsum = _mm_add_epi8(prfsum, _mm_srli_si128(prfsum, 2));
	prfsum = _mm_add_epi8(prfsum, _mm_srli_si128(prfsum, 4));
	prfsum = _mm_add_epi8(prfsum, _mm_srli_si128(prfsum, 8));

	// isolate sequences of blanks and count their individual lengths, right to left, using a prefix max and the above prefix sum
	__m128i prfmax = _mm_and_si128(pos, prfsum);
	prfmax = _mm_max_epu8(prfmax, _mm_srli_si128(prfmax, 1));
	prfmax = _mm_max_epu8(prfmax, _mm_srli_si128(prfmax, 2));
	prfmax = _mm_max_epu8(prfmax, _mm_srli_si128(prfmax, 4));
	prfmax = _mm_max_epu8(prfmax, _mm_srli_si128(prfmax, 8));
	prfmax = _mm_sub_epi8(prfsum, prfmax);

	// add blank counts to a sequential index to get non-blanks index
	__m128i const index = _mm_add_epi8(_mm_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15), prfmax);

	// cap trailing out-of-bounds in the index (non-mandatory)
	__m128i const indey = _mm_or_si128(index, _mm_cmpgt_epi8(index, _mm_set1_epi8(15)));

	// use the index to fetch all non-blanks from the dictionary
	__m128i const res = _mm_shuffle_epi8(vin, indey);

	_mm_storeu_si128(reinterpret_cast< __m128i* >(output), res);
}

#endif
#if __SSSE3__
// pruner -- full-fledged; SSSE3 version
inline void testee04() {
}

#endif
int main(int, char**) {
	size_t const rep = size_t(5e7);

	for (size_t i = 0; i < rep; ++i) {

#if TESTEE == 4
		testee04();

#elif TESTEE == 3
		testee03();

#elif TESTEE == 2
		testee02();

#elif TESTEE == 1
		testee01();

#else
		testee00();

#endif
		// iteration obfuscator
		asm volatile ("" : : : "memory");
	}

#if TESTEE
	fprintf(stderr, "%.32s\n", output + misalign);

#else
	fprintf(stderr, "%.32s\n", output);

#endif
	return 0;
}

