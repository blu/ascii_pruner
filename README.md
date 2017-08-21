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
Apparently the output string is missing its nil-termination, but that is a trivial operation which does not change the performance characteristics of the code. This holds true for the rest of the routines in this survey.

I ran `testee00` on a bunch of amd64 CPUs and a few arm64 CPUs, using different GCC and Clang compiler versions, always taking the best compiler result. Here are the clocks/character results, computed from `perf -e cycles` divided by the number of processed chars (in our case - 5 * 10^7 * 16), and truncated to the 4th digit after the decimal point:


| CPU                          | Compiler & codegen flags           | clocks/character |
| ---------------------------- | ---------------------------------- | ---------------- |
| Intel Xeon E5-2687W (SNB)    | g++-4.8 -Ofast                     | 1.6363           |
| Intel Xeon E3-1270v2 (IVB)   | g++-5.1 -Ofast                     | 1.6186           |
| Intel i7-5820K (HSW)         | g++-4.8 -Ofast                     | 1.5223           |
| AMD Ryzen 7 1700 (Zen)       | g++-5.4 -Ofast                     | 1.4113           |
| Marvell 8040 (Cortex-A72)    | g++-5.4 -Ofast                     | 1.3805           |

Table 1. Performance of `testee00` on desktop-level cores

Interesting, isn’t it - ARM's 'big' A72 core (3-decode, 8-issue OoO) actually does better clocks/char than the wider desktop chips (you can see the actual perf stat sessions at the end of this writing). It's as if the ISA of the ARM (A64) is better suited for the task (see the instruction counts in the perf logs below) and the IPC of the A72, while lower, is still sufficiently high to out-perform the Intels per clock.

So, let’s move on to SIMD. Now, I don’t claim to be a seasoned NEON coder, but I get my hands dirty with ARM SIMD occasionally. I will not inline the SIMD routines here since that would choke the reader; instead, the entire testbed and participating test routines can be found in the supplied code.

Daniel’s original SSSE3 pruning routine uses a look-up table to do the input vector sampling. But I used another algorithm for the test. The reason? I cannot easily take 2^16 * 2^4 = 1MB look-up tables in my code - that would be a major cache thrasher for any scenarios where we don’t just prune ascii streams, but call the routine amids other work. The LUT-less SSSE3 version comes at the price of a tad more computations, but runs entirely off registers, and as you’ll see, the price for dropping the table is not prohibitive even on sheer pruning workloads. Moreover, both the new SSSE3 version and the NEON (ASIMD2) version use the same algorithm now, so the comparison is as direct as physically possible. And lastly, all tests run entirely off L1 cache.

| CPU                          | Compiler & codegen flags            | clocks/character |
| ---------------------------- | ----------------------------------- | ---------------- |
| Intel Xeon E5-2687W (SNB)    | clang++-3.9 -Ofast -mssse3 -mpopcnt | .9268            |
| Intel Xeon E3-1270v2 (IVB)   | clang++-3.7 -Ofast -mssse3 -mpopcnt | .8223            |
| Intel i7-5820K (HSW)         | clang++-3.9 -Ofast -mavx2           | .8232       [^1] |
| AMD Ryzen 7 1700 (Zen)       | clang++-4.0 -Ofast -mssse3 -mpopcnt | .6671       [^2] |
| Marvell 8040 (Cortex-A72)    | clang++-3.6 -Ofast -mcpu=cortex-a57 | 1.4603      [^3] |

Table 2. Performance of `testee04` on desktop-level cores

[^1]: AVX2-128 used for Haswell, as the same intrinsics work with the newer target, while producing better results than SSSE3.  
[^2]: SSSE3 yields significantly better IPC than AVX2 on Ryzen, so that the overall performance of SSSE3 code is better, despite the increased instruction count.  
[^3]: Version used here is `testee07` - a twice-wider (32-batch) version of `testee04`; that still proves insufficient to fully counter the pipeline bubbles brought about by A72's ASIMD latencies.

As you see, the per-clock efficiency advantage is approx. 2x for the desktop amd64 cores - cores that at the same (or similar) fabnode would be 4x the area of the A72. That said, the employed SIMD algorithm does not perform well on A72; actually, A72's SIMD does not scale at all, let alone nearly as good as Intel's or AMD's, with this algorithm. This appears to be due to an uarch issue with A72 - its SIMD exhibits high latencies for the permutation ops employed by our algorithm. As a result, the scalar version performs better than the 32-wide SIMD version! As we will see below, that is not the case with other ARMv8 uarchs, though.

Same test on entry-level arm64 and amd64 CPUs:

| CPU                          | Compiler & codegen flags                            | clocks/character |
| ---------------------------- | --------------------------------------------------- | ---------------- |
| AMD C60 (Bobcat)             | g++-4.8 -Ofast                                      | 3.5733           |
| MediaTek MT8163 (Cortex-A53) | clang++-3.6 -Ofast -mcpu=cortex-a53                 | 2.6568           |

Table 3. Performance of `testee00` on entry-level cores

| CPU                          | Compiler & codegen flags                            | clocks/character |
| ---------------------------- | --------------------------------------------------- | ---------------- |
| AMD C60 (Bobcat)             | clang++-3.7 -Ofast -mssse3 -mpopcnt                 | 4.3284      [^4] |
| MediaTek MT8163 (Cortex-A53) | clang++-3.8 -Ofast -mcpu=cortex-a53                 | 2.0850      [^5] |

Table 4. Performance of `testee04` on entry-level cores

[^4]: Bobcat (btver1) experiences Death by popcnt™ here; Jaguar (btver2) does not suffer from that, but part is hard to get ahold of.  
[^5]: Version used here is `testee06` - the arm64 equivalent of `testee04`.

Going wider, from 16-barch to 32-batch:

| CPU                          | Compiler & codegen flags                            | clocks/character |
| ---------------------------- | --------------------------------------------------- | ---------------- |
| AMD C60 (Bobcat)             |                                                     | TODO             |
| MediaTek MT8163 (Cortex-A53) | clang++-3.8 -Ofast -mcpu=cortex-a53                 | 1.4559           |

Table 5. Performance of `testee07` on entry-level cores

Surprisingly enough, the per-clock efficiency of A72 and A53 is nearly identical on this test. But don't let this fool you into thinking that the integer SIMD pipelines of the two uarchs are identical - that is not the case at all, and the close performance in this scenario is a mere happenensance. This is demonstrated by the fact `testee07` has divergent tuning for A72/A57, on one hand, and for A53 on the other. Perhaps we can compare the A72 to another 'big' core by an architecture licensee?

Same test on Apple's custom cores:

| CPU                          | Compiler & codegen flags           | clocks/character |
| ---------------------------- | ---------------------------------- | ---------------- |
| Apple A7 (Cyclone)           | apple clang++-8.1                  | 1.5101      [^6] |
| Apple A9 (Twister)           | apple clang++-8.1                  | 1.5673      [^6] |

Table 6. Performance of `testee00` on Apple ARMv8

[^6]: For some reason apple's clang does not unroll the inner-most loop, which artificially penalizes this core on this test.

| CPU                          | Compiler & codegen flags           | clocks/character |
| ---------------------------- | ---------------------------------- | ---------------- |
| Apple A7 (Cyclone)           | apple clang++-8.1                  | .9136            |
| Apple A9 (Twister)           | apple clang++-8.1                  | .8608            |

Table 7. Performance of `testee06` on Apple ARMv8

| CPU                          | Compiler & codegen flags           | clocks/character |
| ---------------------------- | ---------------------------------- | ---------------- |
| Apple A7 (Cyclone)           | apple clang++-8.1                  | .6872            |
| Apple A9 (Twister)           | apple clang++-8.1                  | .6552            |

Table 8. Performance of `testee07` on Apple ARMv8

Clearly Apple's uarchs behave quite differently to ARM's A72 - their behaviour in this test is much more in-line with the desktop chips and the 'little' A53, leaving the A72 as the outlier at this SIMD algorithm. All thanks to "outstanding" permute latencies.

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
SSSE3 version, 16-batch
```
$ clang++-3.9 -Ofast -mssse3 -mpopcnt prune.cpp -DTESTEE=4
$ perf stat -e task-clock,cycles,instructions -- ./a.out
0123456789abc

 Performance counter stats for './a.out':

        238.949065      task-clock (msec)         #    0.998 CPUs utilized
       741,459,257      cycles                    #    3.103 GHz
     2,102,528,489      instructions              #    2.84  insns per cycle

       0.239405193 seconds time elapsed

$ echo "scale=4; 741459257 / (5 * 10^7 * 16)" | bc
.9268
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
SSSE3 version, 16-batch
```
$ clang++-3.7 -Ofast prune.cpp -mssse3 -mpopcnt -DTESTEE=4
$ perf stat -e task-clock,cycles,instructions -- ./a.out
0123456789abc

 Performance counter stats for './a.out':

        411.788221 task-clock (msec)         #    0.998 CPUs utilized
       657,871,749 cycles                    #    1.598 GHz
     2,102,394,157 instructions              #    3.20  insns per cycle

       0.412536582 seconds time elapsed

$ echo "scale=4; 657871749 / (5 * 10^7 * 16)" | bc
.8223
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
AVX2-128 version, 16-batch
```
$ clang++-3.9 -Ofast prune.cpp -mavx2 -DTESTEE=4
$ perf stat -e task-clock,cycles,instructions -- ./a.out
0123456789abc

 Performance counter stats for './a.out':

        185.266773      task-clock (msec)         #    0.998 CPUs utilized
       658,574,857      cycles                    #    3.555 GHz
     1,652,476,471      instructions              #    2.51  insn per cycle

       0.185700333 seconds time elapsed

$ echo "scale=4; 658574857 / (5 * 10^7 * 16)" | bc
.8232
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
SSSE3 version, 16-batch
```
$ clang++-4 -Ofast prune.cpp -mssse3 -mpopcnt -DTESTEE=4
$ perf stat -e task-clock,cycles,instructions -- ./a.out
0123456789abc

 Performance counter stats for './a.out':

        168,212672      task-clock:u (msec)       #    0,999 CPUs utilized
         533680965      cycles:u                  #    3,173 GHz
        2102126854      instructions:u            #    3,94  insn per cycle

       0,168401579 seconds time elapsed

$ echo "scale=4; 533680965 / (5 * 10^7 * 16)" | bc
.6671
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
ASIMD2 version, 32-batch
```
$ clang++-3.6 prune.cpp -Ofast -mcpu=cortex-a57 -DTESTEE=7
$ perf stat -e task-clock,cycles,instructions -- ./a.out
0123456789abcdef123456789abc

 Performance counter stats for './a.out':

       1797.414520      task-clock (msec)         #    1.000 CPUs utilized
     2,336,626,281      cycles                    #    1.300 GHz
     2,954,747,281      instructions              #    1.26  insns per cycle

       1.798039048 seconds time elapsed

$ echo "scale=4; 2336626281 / (5 * 10^7 * 32)" | bc
1.4603
```
---
AMD C60 (Bobcat) @ 1.333GHz

Scalar version
```
$ g++-4.8 prune.cpp -Ofast
$ perf stat -e task-clock,cycles,instructions -- ./a.out
alabalanica1234

 Performance counter stats for './a.out':

       2151.161881 task-clock (msec)         #    0.998 CPUs utilized
     2,858,689,394 cycles                    #    1.329 GHz
     4,602,837,331 instructions              #    1.61  insns per cycle

       2.154862906 seconds time elapsed

$ echo "scale=4; 2858689394 / (5 * 10^7 * 16)" | bc
3.5733
```
SSSE3 version, 16-batch
```
$ clang++-3.7 -Ofast -mssse3 -mpopcnt prune.cpp -DTESTEE=4
$ perf stat -e task-clock,cycles,instructions -- ./a.out
0123456789abc

 Performance counter stats for './a.out':

       2608.381080 task-clock (msec)         #    0.998 CPUs utilized
     3,462,731,680 cycles                    #    1.328 GHz
     2,104,783,519 instructions              #    0.61  insns per cycle

       2.612564016 seconds time elapsed

$ echo "scale=4; 3462731680 / (5 * 10^7 * 16)" | bc
4.3284
```
---
MediaTek MT8163 (Cortex-A53) @ 1.50GHz (sans perf)

Scalar version
```
$ clang++-3.6 -Ofast prune.cpp -mcpu=cortex-a53
$ time ./a.out
alabalanica1234

real    0m1.417s
user    0m1.410s
sys     0m0.000s
$ echo "scale=4; 1.417 * 1.5 * 10^9 / (5 * 10^7 * 16)" | bc
2.6568
```
ASIMD2 version, 16-batch
```
$ clang++-3.8 -Ofast prune.cpp -mcpu=cortex-a53 -DTESTEE=6
$ time ./a.out
0123456789abc

real    0m1.112s
user    0m1.100s
sys     0m0.000s
$ echo "scale=4; 1.112 * 1.5 * 10^9 / (5 * 10^7 * 16)" | bc
2.0850
```
ASIMD2 version, 32-batch
```
$ clang++-3.8 -Ofast prune.cpp -mcpu=cortex-a53 -DTESTEE=7 -DSAME_LATENCY_Q_AND_D
$ time ./a.out
0123456789abcdef123456789abc

real    0m1.553s
user    0m1.540s
sys     0m0.000s
$ echo "scale=4; 1.553 * 1.5 * 10^9 / (5 * 10^7 * 32)" | bc
1.4559
```
---
Apple A7 (Cyclone) @ 1.30GHz (sans perf)

Scalar version
```
$ echo "scale=4; 0.929309 * 1.3 * 10^9 / (5 * 10^7 * 16)" | bc
1.5101
```
ASIMD2 version, 16-batch
```
$ echo "scale=4; 0.562230 * 1.3 * 10^9 / (5 * 10^7 * 16)" | bc
.9136
```
ASIMD2 version, 32-batch
```
$ echo "scale=4; 0.845820 * 1.3 * 10^9 / (5 * 10^7 * 32)" | bc
.6872
```
---
Apple A9 (Twister) @ 1.85GHz (sans perf)

Scalar version
```
$ echo "scale=4; 0.677764 * 1.85 * 10^9 / (5 * 10^7 * 16)" | bc
1.5673
```
ASIMD2 version, 16-batch
```
$ echo "scale=4; 0.372241 * 1.85 * 10^9 / (5 * 10^7 * 16)" | bc
.8608
```
ASIMD2 version, 32-batch
```
$ echo "scale=4; 0.566721 * 1.85 * 10^9 / (5 * 10^7 * 32)" | bc
.6552
```
