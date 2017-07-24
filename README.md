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

Interesting, isn’t it - the little ARM (3-decode, 8-issue OoO) actually does better clocks/char than the wider desktop chips (you can see the actual perf stat sessions at the end of this writing). It's as if the ISA of the ARM (A64) is better suited for the task (see the instruction counts in the perf logs below) and the IPC of the A72, while lower, is still sufficiently high to out-perform the Intels per clock.

So, let’s move on to SIMD. Now, I don’t claim to be a seasoned NEON coder, but I get my hands dirty with ARM SIMD occasionally. I will not inline the SIMD routines here since that would choke the reader; instead, the entire testbed and participating test routines can be found in the supplied code.

I took the liberty to change Daniel’s original SSSE3 pruning routine - actually, I used my version for the test. The reason? I cannot easily take 2^16 * 2^4 = 1MB look-up tables in my code - that would be a major cache thrasher for any scenarios where we don’t just prune ascii streams, but call the routine amids other work. The LUT-less SSSE3 version comes at the price of a tad more computations, but runs entirely off registers, and as you’ll see, the price for dropping the table is not prohibitive even on sheer pruning workloads. Moreover, both the new SSSE3 version and the NEON (ASIMD2) version use the same algorithm now, so the comparison is as direct as physically possible. And lastly, all tests run entirely off L1 cache.

| CPU                          | Compiler & flags                   | clocks/character |
| ---------------------------- | ---------------------------------- | ---------------- |
| Intel Xeon E5-2687W (SNB)    | g++-4.8 -Ofast -mssse3             | .4230            |
| Intel Xeon E3-1270v2 (IVB)   | g++-5.4 -Ofast -mssse3             | .3774            |
| Marvell 8040 (Cortex-A72)    | g++-5.4 -Ofast -mcpu=cortex-a57    | 1.0503           |

Table 2. Performance of `testee01` on desktop-level cores

Note: uarch tuning for A57 passed to the arm64 build since the compiler’s generic scheduler is openly worse in this version when it comes to NEON code, and A57 is a fairly “generic” ARMv8 common denominator when it comes to scheduling.

As you see, the per-clock efficiency advantage is 2.5x for Sandy Bridge and 2.8x for Ivy Bridge, respectively - cores that at the same (or similar) fabnode would be 4x the area of the A72. So things don’t look so bad for the ARM chips after all!

Bonus material: same test on entry-level arm64 and amd64 CPUs:

| CPU                          | Compiler & flags                                    | clocks/character, scalar | clocks/character, vector |
| ---------------------------- | --------------------------------------------------- | ------------------------ | ------------------------ |
| AMD C60 (Bobcat)             | g++-4.8 -Ofast -mssse3                              | 3.5751                   | 1.8215                   |
| MediaTek MT8163 (Cortex-A53) | clang++-3.6 -march=armv8-a -mtune=cortex-a53 -Ofast | 2.6568                   | 1.7100                   |

Table 3. Performance of `testee00` and `testee01` on entry-level cores

---
Xeon E5-2687W @ 3.10GHz

Scalar version
```
$ g++-4.8 prune.cpp -Ofast
$ perf stat -e task-clock,cycles,instructions -- ./a.out
alabalanica1234

 Performance counter stats for './a.out':

        421.886991      task-clock (msec)         #    0.998 CPUs utilized
     1,309,087,898      cycles                    #    3.103 GHz
     4,603,132,268      instructions              #    3.52  insns per cycle

       0.422602570 seconds time elapsed

$ echo "scale=4; 1309087898 / (5 * 10^7 * 16)" | bc
1.6363
```
SSSE3 version (batch of 16, misaligned write)
```
$ g++-4.8 prune.cpp -Ofast -mssse3
$ perf stat -e task-clock,cycles,instructions -- ./a.out
alabalanica1234a

 Performance counter stats for './a.out':

        109.063426      task-clock (msec)         #    0.997 CPUs utilized
       338,414,215      cycles                    #    3.103 GHz
     1,052,118,398      instructions              #    3.11  insns per cycle

       0.109422808 seconds time elapsed

$ echo "scale=4; 338414215 / (5 * 10^7 * 16)" | bc
.4230
```
---
Xeon E3-1270v2 @ 1.60GHz

Scalar version
```
$ g++-5 -Ofast prune.cpp
$ perf stat -e task-clock,cycles,instructions -- ./a.out
alabalanica1234

 Performance counter stats for './a.out':

        810.515709 task-clock (msec)         #    0.999 CPUs utilized
     1,294,903,960 cycles                    #    1.598 GHz
     4,601,118,631 instructions              #    3.55  insns per cycle

       0.811646618 seconds time elapsed

$ echo "scale=4; 1294903960 / (5 * 10^7 * 16)" | bc
1.6186
```
SSSE3 version (batch of 16, misaligned write)
```
$ g++-5 -Ofast prune.cpp -mssse3
$ perf stat -e task-clock,cycles,instructions -- ./a.out
alabalanica1234a

 Performance counter stats for './a.out':

        188.995814 task-clock (msec)         #    0.997 CPUs utilized
       301,931,101 cycles                    #    1.598 GHz
     1,050,607,539 instructions              #    3.48  insns per cycle

       0.189536527 seconds time elapsed

$ echo "scale=4; 301931101 / (5 * 10^7 * 16)" | bc
.3774
```
---
Intel i7-5820K

Scalar version
```
$ g++-4.8 -Ofast prune.cpp
$ perf stat -e task-clock,cycles,instructions -- ./a.out
alabalanica1234

 Performance counter stats for './a.out':

        339.202545      task-clock (msec)         #    0.997 CPUs utilized
     1,204,872,493      cycles                    #    3.552 GHz
     4,602,943,398      instructions              #    3.82  insn per cycle

       0.340089829 seconds time elapsed

$ echo "scale=4; 1204872493 / (5 * 10^7 * 16)" | bc
1.5060
```
---
AMD Ryzen 7 1700

Scalar version
```
$ perf stat -e task-clock,cycles,instructions -- ./a.out
alabalanica1234

 Performance counter stats for './a.out':

        356,169901      task-clock:u (msec)       #    0,999 CPUs utilized
        1129098820      cycles:u                  #    3,170 GHz
        4602126161      instructions:u            #    4,08  insn per cycle

       0,356353748 seconds time elapsed

$ echo "scale=4; 1129098820 / (5 * 10^7 * 16)" | bc
1.4113
```
---
Marvell ARMADA 8040 (Cortex-A72) @ 1.30GHz

Scalar version
```
$ g++-5 prune.cpp -Ofast
$ perf stat -e task-clock,cycles,instructions -- ./a.out
alabalanica1234

 Performance counter stats for './a.out':

        849.549040      task-clock (msec)         #    0.999 CPUs utilized
     1,104,405,671      cycles                    #    1.300 GHz
     3,251,212,918      instructions              #    2.94  insns per cycle

       0.850107930 seconds time elapsed

$ echo "scale=4; 1104405671 / (5 * 10^7 * 16)" | bc
1.3805
```
ASIMD2 version (batch of 16, misaligned write)
```
$ g++-5 prune.cpp -Ofast -mcpu=cortex-a57 -mtune=cortex-a57
$ perf stat -e task-clock,cycles,instructions -- ./a.out
alabalanica1234

 Performance counter stats for './a.out':

        646.394560      task-clock (msec)         #    0.999 CPUs utilized
       840,305,966      cycles                    #    1.300 GHz
       801,000,092      instructions              #    0.95  insns per cycle

       0.646946289 seconds time elapsed

$ echo "scale=4; 840305966 / (5 * 10^7 * 16)" | bc
1.0503
```
ASIMD2 version (batch of 32, misaligned write)
```
$ clang++-3.7 prune.cpp -Ofast -mcpu=cortex-a57 -mtune=cortex-a57
$ perf stat -e task-clock,cycles,instructions -- ./a.out
alabalanica1234

 Performance counter stats for './a.out':

       1140.643640      task-clock (msec)         #    0.999 CPUs utilized
     1,482,826,308      cycles                    #    1.300 GHz
     1,504,011,807      instructions              #    1.01  insns per cycle

       1.141241760 seconds time elapsed

$ echo "scale=4; 1482826308 / (5 * 10^7 * 32)" | bc
.9267
```
---
AMD C60 (Bobcat) @ 1.333GHz

Scalar version
```
$ g++-4.8 prune.cpp -Ofast
$ perf stat -e task-clock,cycles,instructions -- ./a.out
alabalanica1234

 Performance counter stats for './a.out':

       2208.190651 task-clock (msec)         #    0.997 CPUs utilized
     2,860,081,604 cycles                    #    1.295 GHz
     4,602,968,860 instructions              #    1.61  insns per cycle

       2.214173331 seconds time elapsed

$ echo "scale=4; 2860081604 / (5 * 10^7 * 16)" | bc
3.5751
```
SSSE3 version (batch of 16, misaligned write)
```
$ clang++-3.5 prune.cpp -Ofast -mssse3
$ perf stat -e task-clock,cycles,instructions -- ./a.out
alabalanica1234a

 Performance counter stats for './a.out':

       1098.519499 task-clock (msec)         #    0.998 CPUs utilized
     1,457,266,396 cycles                    #    1.327 GHz
     1,053,073,591 instructions              #    0.72  insns per cycle

       1.101240320 seconds time elapsed

$ echo "scale=4; 1457266396 / (5 * 10^7 * 16)" | bc
1.8215
```
---
MediaTek MT8163 (Cortex-A53) @ 1.50GHz (sans perf)

Scalar version
```
$ ../clang+llvm-3.6.2-aarch64-linux-gnu/bin/clang++ prune.cpp -march=armv8-a -mtune=cortex-a53 -Ofast
$ time ./a.out
alabalanica1234

real    0m1.417s
user    0m1.410s
sys     0m0.000s
$ echo "scale=4; 1.417 * 1.5 * 10^9 / (5 * 10^7 * 16)" | bc
2.6568
```
ASIMD2 version (batch of 16, misaligned write)
```
$ ../clang+llvm-3.6.2-aarch64-linux-gnu/bin/clang++ prune.cpp -march=armv8-a -mtune=cortex-a53 -Ofast
$ time ./a.out
alabalanica1234

real    0m0.912s
user    0m0.900s
sys     0m0.000s
$ echo "scale=4; 0.912 * 1.5 * 10^9 / (5 * 10^7 * 16)" | bc
1.7100
```
