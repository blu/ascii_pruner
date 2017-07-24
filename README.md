A friend of mine brought to my attention an interesting article on habrahabr.ru - [a Russian translation](https://habrahabr.ru/post/332710/) of Daniel Lemire’s [Pruning Spaces from Strings Quickly on ARM Processors](http://lemire.me/blog/2017/07/03/pruning-spaces-from-strings-quickly-on-arm-processors/). That article intrigued me for two reasons: first, somebody had actually made an effort to seek an optimal solution of a common problem on a non-x86 architecture (yay!), and second, the results the author presented at the end of his article puzzled me: nearly 6x per-clock advantage for Intel? That was emphasized by the author’s conclusion that perhaps ARM couldn’t possibly be nearly as clock-efficient as the ‘big iron’ Intel on this simple task.

Well, challenge accepted!

The author had started with a baseline - a serial implementation, so I too decided to start from there and move up. Let’s call this baseline `testee00` and get familiar with it before we move on:

```c
inline void testee00() {
    size_t i = 0, pos = 0;
    while (i < 16) {
        const char c = input[i++];
        output[pos] = c;
        pos += (c > 32 ? 1 : 0);
    }
}
```

I ran `testee00` on a bunch amd64 CPUs and one arm64 CPU, using different GCC and Clang compiler versions, always taking the best compiler result. Here are the clocks/character results, computed from `perf -e cycles` divided by the number of processed chars (in our case - 5 * 10^7 * 16), and truncated to the 4th digit after the decimal point:


| CPU                          | Compiler & flags                   | clocks/character |
| ---------------------------- | ---------------------------------- | ---------------- |
| Intel Xeon E5-2687W (SNB)    | g++-4.8 -Ofast                     | 1.6363           |
| Intel Xeon E3-1270v2 (IVB)   | g++-5.1 -Ofast                     | 1.6186           |
| Intel i7-5820K (HSW)         | g++-4.8 -Ofast                     | 1.5223           |
| AMD Ryzen 7 1700 (Zen)       | g++-5.4 -Ofast                     | 1.4113           |
| Marvell 8040 (Cortex-A72)    | g++-5.4 -Ofast                     | 1.3805           |

Table 1. Performance of `testee00` on desktop-level cores

Interesting, isn’t it - the little ARM (3-decode, 8-issue OoO) actually does better clocks/char than the wider desktop chips (you can see the actual perf stat sessions at the end of this writing).

So, let’s move on to SIMD. Now, I don’t claim to be a seasoned NEON coder, but I get my hands dirty with ARM SIMD occasionally. I will not inline the SIMD routines here since that would choke the reader; instead, the entire testbed and participating test routines can be found in the supplied code.

I took the liberty to change Daniel’s original SSSE3 pruning routine - actually, I used my version for the test. The reason? I cannot easily take 2^16 * 2^4 = 1MB look-up tables in my code - that would be a major cache thrasher for any scenarios where we don’t just prune ascii streams, but call the routine amids other work. The LUT-less SSSE3 version comes at the price of a tad more computations, but runs entirely off registers, and as you’ll see, the price for dropping the table is not prohibitive even on sheer pruning workloads. Moreover, both the new SSSE3 version and the NEON (ASIMD2) version use the same algorithm now, so the comparison is as direct as physically possible. And lastly, all tests run entirely off L1 cache.

| CPU                          | Compiler & flags                   | clocks/character |
| ---------------------------- | ---------------------------------- | ---------------- |
| Intel Xeon E5-2687W (SNB)    | g++-4.8 -Ofast -mssse3             | .4230            |
| Intel Xeon E3-1270v2 (IVB)   | g++-5.4 -Ofast -mssse3             | .3774            |
| Marvell 8040 (Cortex-A72)    | g++-5.4 -Ofast -mcpu=cortex-a57    | 1.0503           |

Table 2. Performance of `testee01` on desktop-level cores

* Micro-arch tuning for A57 passed to the arm64 build since the compiler’s generic scheduler is openly worse in this version when it comes to NEON code, and A57 is a fairly “generic” ARMv8 common denominator when it comes to scheduling.

As you see, the per-clock efficiency advantage is 2.5x for Sandy Bridge and 2.8x for Ivy Bridge, respectively - cores that at the same (or similar) fabnode would be 4x the area of the A72. So things don’t look so bad for the ARM chips after all!

Bonus material: same test on entry-level arm64 and amd64 CPUs:

| CPU                          | Compiler & flags                                    | clocks/character, scalar | clocks/character, vector |
| ---------------------------- | --------------------------------------------------- | ------------------------ | ------------------------ |
| AMD C60 (Bobcat)             | g++-4.8 -Ofast -mssse3                              | 3.5751                   | 1.8215                   |
| MediaTek MT8163 (Cortex-A53) | clang++-3.6 -march=armv8-a -mtune=cortex-a53 -Ofast | 2.6568                   | 1.7100                   |

Table 3. Performance of `testee00` and `testee01` on entry-level cores

