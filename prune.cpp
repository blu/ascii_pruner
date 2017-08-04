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

uint8_t input[32] __attribute__ ((aligned(16))) = "012345 789abcdef 123456789abcde";
uint8_t output[32] __attribute__ ((aligned(16)));

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
// naive pruner, 16-batch; filter single blank from N input chars, followed by K optional trailing blanks, N + K = batch size
// example: "1234 678  " -> "1234678" (N + K = 10)
inline size_t testee01() {
	uint8x16_t const vinput = vld1q_u8(input);
	uint8x16_t prfsum = vcleq_u8(vinput, vdupq_n_u8(' '));

	// pick one:
	// before computing the prefix sum: count_of_blanks := vaddvq_u8(prfsum)
	// after computing the prefix sum: count_of_blanks := prfsum[last_lane]
	prfsum = vaddq_u8(prfsum, vextq_u8(vdupq_n_u8(0), prfsum, 16 - 1));
	prfsum = vaddq_u8(prfsum, vextq_u8(vdupq_n_u8(0), prfsum, 16 - 2));
	prfsum = vaddq_u8(prfsum, vextq_u8(vdupq_n_u8(0), prfsum, 16 - 4));
	prfsum = vaddq_u8(prfsum, vextq_u8(vdupq_n_u8(0), prfsum, 16 - 8));

	int8_t const bnum = vgetq_lane_u8(prfsum, 15);

	uint8x16_t const index = vsubq_u8((uint8x16_t) { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }, prfsum);
	uint8x16_t const res = vqtbl1q_u8(vinput, index);

	vst1q_u8(output, res);
	return sizeof(uint8x16_t) + bnum;
}

// naive pruner, 32-batch; filter single blank from N input chars, followed by K optional trailing blanks, N + K = half batch size
// example: "1234 678  " -> "1234678" (N + K = 10)
inline size_t testee02() {
	uint8x16_t const vinput0 = vld1q_u8(input);
	uint8x16_t const vinput1 = vld1q_u8(input + sizeof(uint8x16_t));
	uint8x16_t prfsum0 = vcleq_u8(vinput0, vdupq_n_u8(' '));
	uint8x16_t prfsum1 = vcleq_u8(vinput1, vdupq_n_u8(' '));

	// pick one:
	// before computing the prefix sum: count_of_blanks := vaddvq_u8(prfsum)
	// after computing the prefix sum: count_of_blanks := prfsum[last_lane]
	prfsum0 = vaddq_u8(prfsum0, vextq_u8(vdupq_n_u8(0), prfsum0, 16 - 1));
	prfsum1 = vaddq_u8(prfsum1, vextq_u8(vdupq_n_u8(0), prfsum1, 16 - 1));
	prfsum0 = vaddq_u8(prfsum0, vextq_u8(vdupq_n_u8(0), prfsum0, 16 - 2));
	prfsum1 = vaddq_u8(prfsum1, vextq_u8(vdupq_n_u8(0), prfsum1, 16 - 2));
	prfsum0 = vaddq_u8(prfsum0, vextq_u8(vdupq_n_u8(0), prfsum0, 16 - 4));
	prfsum1 = vaddq_u8(prfsum1, vextq_u8(vdupq_n_u8(0), prfsum1, 16 - 4));
	prfsum0 = vaddq_u8(prfsum0, vextq_u8(vdupq_n_u8(0), prfsum0, 16 - 8));
	prfsum1 = vaddq_u8(prfsum1, vextq_u8(vdupq_n_u8(0), prfsum1, 16 - 8));

	int8_t const bnum0 = vgetq_lane_u8(prfsum0, 15);
	int8_t const bnum1 = vgetq_lane_u8(prfsum1, 15);

	uint8x16_t const index0 = vsubq_u8((uint8x16_t) { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }, prfsum0);
	uint8x16_t const index1 = vsubq_u8((uint8x16_t) { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }, prfsum1);
	uint8x16_t const res0 = vqtbl1q_u8(vinput0, index0);
	uint8x16_t const res1 = vqtbl1q_u8(vinput1, index1);

	vst1q_u8(output, res0);
	vst1q_u8(output + sizeof(uint8x16_t) + bnum0, res1);
	return sizeof(uint8x16_t) * 2 + bnum0 + bnum1;
}

#elif __SSSE3__
// naive pruner, 16-batch; filter single blank from N input chars, followed by K optional trailing blanks, N + K = batch size
// example: "1234 678  " -> "1234678" (N + K = 10)
inline size_t testee01() {
	__m128i const vinput = _mm_load_si128(reinterpret_cast< const __m128i* >(input));
	__m128i prfsum = _mm_cmplt_epi8(vinput, _mm_set1_epi8(' ' + 1));

	// pick one:
	// before computing the prefix sum: count_of_blanks := _mm_popcnt_u32(_mm_movemask_epi8(prfsum))
	// after computing the prefix sum: count_of_blanks := prfsum[last_lane]
	prfsum = _mm_add_epi8(prfsum, _mm_slli_si128(prfsum, 1));
	prfsum = _mm_add_epi8(prfsum, _mm_slli_si128(prfsum, 2));
	prfsum = _mm_add_epi8(prfsum, _mm_slli_si128(prfsum, 4));
	prfsum = _mm_add_epi8(prfsum, _mm_slli_si128(prfsum, 8));

	int8_t const bnum = uint16_t(_mm_extract_epi16(prfsum, 7)) >> 8;

	__m128i const index = _mm_sub_epi8(_mm_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15), prfsum);
	__m128i const res = _mm_shuffle_epi8(vinput, index);

	_mm_storeu_si128(reinterpret_cast< __m128i* >(output), res);
	return sizeof(__m128i) + bnum;
}

// naive pruner, 32-batch; filter single blank from N input chars, followed by K optional trailing blanks, N + K = half batch size
// example: "1234 678  " -> "1234678" (N + K = 10)
inline size_t testee02() {
	__m128i const vinput0 = _mm_load_si128(reinterpret_cast< const __m128i* >(input));
	__m128i const vinput1 = _mm_load_si128(reinterpret_cast< const __m128i* >(input) + 1);
	__m128i prfsum0 = _mm_cmplt_epi8(vinput0, _mm_set1_epi8(' ' + 1));
	__m128i prfsum1 = _mm_cmplt_epi8(vinput1, _mm_set1_epi8(' ' + 1));

	// pick one:
	// before computing the prefix sum: count_of_blanks := _mm_popcnt_u32(_mm_movemask_epi8(prfsum))
	// after computing the prefix sum: count_of_blanks := prfsum[last_lane]
	prfsum0 = _mm_add_epi8(prfsum0, _mm_slli_si128(prfsum0, 1));
	prfsum1 = _mm_add_epi8(prfsum1, _mm_slli_si128(prfsum1, 1));
	prfsum0 = _mm_add_epi8(prfsum0, _mm_slli_si128(prfsum0, 2));
	prfsum1 = _mm_add_epi8(prfsum1, _mm_slli_si128(prfsum1, 2));
	prfsum0 = _mm_add_epi8(prfsum0, _mm_slli_si128(prfsum0, 4));
	prfsum1 = _mm_add_epi8(prfsum1, _mm_slli_si128(prfsum1, 4));
	prfsum0 = _mm_add_epi8(prfsum0, _mm_slli_si128(prfsum0, 8));
	prfsum1 = _mm_add_epi8(prfsum1, _mm_slli_si128(prfsum1, 8));

	int8_t const bnum0 = uint16_t(_mm_extract_epi16(prfsum0, 7)) >> 8;
	int8_t const bnum1 = uint16_t(_mm_extract_epi16(prfsum1, 7)) >> 8;

	__m128i const index0 = _mm_sub_epi8(_mm_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15), prfsum0);
	__m128i const index1 = _mm_sub_epi8(_mm_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15), prfsum1);
	__m128i const res0 = _mm_shuffle_epi8(vinput0, index0);
	__m128i const res1 = _mm_shuffle_epi8(vinput1, index1);

	_mm_storeu_si128(reinterpret_cast< __m128i* >(output), res0);
	_mm_storeu_si128(reinterpret_cast< __m128i* >(output + sizeof(__m128i) + bnum0), res1);
	return sizeof(__m128i) * 2 + bnum0 + bnum1;
}

#endif
#if __SSE2__ && __POPCNT__
// pruner semi, 16-batch; replace blanks with the next non-blank, cutting off trailing blanks from the batch
// example: "1234 678  " -> "12346678"
inline size_t testee03() {
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

	// cap trailing out-of-bounds in the index
	__m128i const indey = _mm_or_si128(index, _mm_cmpgt_epi8(index, _mm_set1_epi8(15)));

	// use the index to fetch all non-blanks from the dictionary
	__m128i const res = _mm_shuffle_epi8(vin, indey);

	_mm_storeu_si128(reinterpret_cast< __m128i* >(output), res);
	return sizeof(__m128i) - _mm_popcnt_u32(_mm_movemask_epi8(indey));
}

#endif
#if __aarch64__
// pruner -- full-fledged
inline size_t testee04() {
	uint8x16_t const vin = vld1q_u8(input);
	uint8x16_t const bmask = vcleq_u8(vin, vdupq_n_u8(' '));

	// OR the mask of all blanks with the original index of the vector
	uint8x16_t const risen = vorrq_u8(bmask, (uint8x16_t) { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 });

	// 16-element sorting network: http://pages.ripco.net/~jgamble/nw.html -- 'Best version'
	// TODO: unoptimised, 'prima-vista' code
	// stage 0
	uint8x16_t const st0a = vqtbl1q_u8(risen, (uint8x16_t) { 0, 2, 4, 6, 8, 10, 12, 14, });
	uint8x16_t const st0b = vqtbl1q_u8(risen, (uint8x16_t) { 1, 3, 5, 7, 9, 11, 13, 15, });
	uint8x16_t const st0min = vminq_u8(st0a, st0b); //  0,  2,  4,  6,  8, 10, 12, 14
	uint8x16_t const st0max = vmaxq_u8(st0a, st0b); //  1,  3,  5,  7,  9, 11, 13, 15

	// stage 1
	uint8x16x2_t const st0 = { { st0min, st0max } };
	uint8x16_t const st1a = vqtbl2q_u8(st0, (uint8x16_t) { 0, 2, 4, 6, 16, 18, 20, 22, });
	uint8x16_t const st1b = vqtbl2q_u8(st0, (uint8x16_t) { 1, 3, 5, 7, 17, 19, 21, 23, });
	uint8x16_t const st1min = vminq_u8(st1a, st1b); //  0,  4,  8, 12,  1,  5,  9, 13
	uint8x16_t const st1max = vmaxq_u8(st1a, st1b); //  2,  6, 10, 14,  3,  7, 11, 15

	// stage 2
	uint8x16x2_t const st1 = { { st1min, st1max } };
	uint8x16_t const st2a = vqtbl2q_u8(st1, (uint8x16_t) { 0, 2, 4, 6, 16, 18, 20, 22, });
	uint8x16_t const st2b = vqtbl2q_u8(st1, (uint8x16_t) { 1, 3, 5, 7, 17, 19, 21, 23, });
	uint8x16_t const st2min = vminq_u8(st2a, st2b); //  0,  8,  1,  9,  2, 10,  3, 11
	uint8x16_t const st2max = vmaxq_u8(st2a, st2b); //  4, 12,  5, 13,  6, 14,  7, 15

	// stage 3
	uint8x16x2_t const st2 = { { st2min, st2max } };
	uint8x16_t const st3a = vqtbl2q_u8(st2, (uint8x16_t) { 0, 2, 4, 6, 16, 18, 20, 22, });
	uint8x16_t const st3b = vqtbl2q_u8(st2, (uint8x16_t) { 1, 3, 5, 7, 17, 19, 21, 23, });
	uint8x16_t const st3min = vminq_u8(st3a, st3b); // 0, 1,  2,  3,  4,  5,  6,  7
	uint8x16_t const st3max = vmaxq_u8(st3a, st3b); // 8, 9, 10, 11, 12, 13, 14, 15

	// from here on some indices are already done -- freeze them, by keeping them in deterministic positions

	// stage 4; indices done so far: 0, 15
	uint8x16x2_t const st3 = { { st3min, st3max } };
	uint8x16_t const st4a = vqtbl2q_u8(st3, (uint8x16_t) {  0,  5,  6,  3, 21,  7, 1,  4, });
	uint8x16_t const st4b = vqtbl2q_u8(st3, (uint8x16_t) { 23, 18, 17, 20, 22, 19, 2, 16, });
	uint8x16_t const st4min = vminq_u8(st4a, st4b); // [ 0],  5,  6,  3, 13,  7,  1,  4
	uint8x16_t const st4max = vmaxq_u8(st4a, st4b); // [15], 10,  9, 12, 14, 11,  2,  8

	// stage 5; done so far: 0, 15; temp frozen: 3, 12
	uint8x16x2_t const st4 = { { st4min, st4max } };
	uint8x16_t const st5a = vqtbl2q_u8(st4, (uint8x16_t) {  0,  3, 6, 5, 22, 21, 1, 18, });
	uint8x16_t const st5b = vqtbl2q_u8(st4, (uint8x16_t) { 16, 19, 7, 4, 23, 20, 2, 17, });
	uint8x16_t const st5min = vminq_u8(st5a, st5b); // [ 0], [ 3], 1,  7, 2, 11, 5,  9
	uint8x16_t const st5max = vmaxq_u8(st5a, st5b); // [15], [12], 4, 13, 8, 14, 6, 10

	// stage 6; done so far: 0, 1, 14, 15; temp frozen: 5, 6, 9, 10
	uint8x16x2_t const st5 = { { st5min, st5max } };
	uint8x16_t const st6a = vqtbl2q_u8(st5, (uint8x16_t) {  0,  2,  4,  5,  1,  3,  6,  7, });
	uint8x16_t const st6b = vqtbl2q_u8(st5, (uint8x16_t) { 16, 21, 18, 19, 20, 17, 22, 23, });
	uint8x16_t const st6min = vminq_u8(st6a, st6b); // [ 0], [ 1], 2, 11, 3,  7, [5], [ 9]
	uint8x16_t const st6max = vmaxq_u8(st6a, st6b); // [15], [14], 4, 13, 8, 12, [6], [10]

	// stage 7; done so far: 0, 1, 2, 13, 14, 15; temp frozen: 4, 11
	uint8x16x2_t const st6 = { { st6min, st6max } };
	uint8x16_t const st7a = vqtbl2q_u8(st6, (uint8x16_t) {  0,  1,  2,  3, 22, 23, 4, 5, });
	uint8x16_t const st7b = vqtbl2q_u8(st6, (uint8x16_t) { 16, 17, 18, 19, 20, 21, 6, 7, });
	uint8x16_t const st7min = vminq_u8(st7a, st7b); // [ 0], [ 1], [2], [11], 6, 10, 3, 7
	uint8x16_t const st7max = vmaxq_u8(st7a, st7b); // [15], [14], [4], [13], 8, 12, 5, 9

	// stage 8; done so far: 0, 1, 2, 13, 14, 15
	uint8x16x2_t const st7 = { { st7min, st7max } };
	uint8x16_t const st8a = vqtbl2q_u8(st7, (uint8x16_t) {  0,  1,  2,  6, 22,  7, 23,  3, });
	uint8x16_t const st8b = vqtbl2q_u8(st7, (uint8x16_t) { 16, 17, 19, 18,  4, 20,  5, 21, });
	uint8x16_t const st8min = vminq_u8(st8a, st8b); // [ 0], [ 1], [ 2], 3, 5, 7,  9, 11
	uint8x16_t const st8max = vmaxq_u8(st8a, st8b); // [15], [14], [13], 4, 6, 8, 10, 12

	// stage 9; done so far: 0, 1, 2, 3, 4, 5, 10, 11, 12, 13, 14, 15
	uint8x16x2_t const st8 = { { st8min, st8max } };
	uint8x16_t const st9a = vqtbl2q_u8(st8, (uint8x16_t) {  0,  1,  2,  3, 19,  4, 20, 21, });
	uint8x16_t const st9b = vqtbl2q_u8(st8, (uint8x16_t) { 16, 17, 18, 23,  7, 22,  5,  6, });
	uint8x16_t const st9min = vminq_u8(st9a, st9b); // [ 0], [ 1], [ 2], [ 3], [ 4], [ 5], 6, 8
	uint8x16_t const st9max = vmaxq_u8(st9a, st9b); // [15], [14], [13], [12], [11], [10], 7, 9

	uint8x16x2_t const st9 = { { st9min, st9max } };
	uint8x16_t const index = vqtbl2q_u8(st9, (uint8x16_t) { 0, 1, 2, 3, 4, 5, 6, 22, 7, 23, 21, 20, 19, 18, 17, 16 });

	uint8x16_t const res = vqtbl1q_u8(vin, index);
	vst1q_u8(output, res);
	return sizeof(uint8x16_t) + int8_t(vaddvq_u8(bmask));
}

#elif __SSSE3__
// pruner -- full-fledged
inline size_t testee04() {
	__m128i const vin = _mm_load_si128(reinterpret_cast< __m128i const* >(input));
	__m128i const bmask = _mm_cmplt_epi8(vin, _mm_set1_epi8(' ' + 1));

	// OR the mask of all blanks with the original index of the vector
	__m128i const risen = _mm_or_si128(bmask, _mm_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15));

	// 16-element sorting network: http://pages.ripco.net/~jgamble/nw.html -- 'Best version'
	// TODO

	return size_t(-1)
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

	fprintf(stderr, "%.32s\n", output);
	return 0;
}

