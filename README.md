# Lazification of Function Arguments

In [strict programming languages](https://en.wikipedia.org/wiki/Evaluation_strategy#Strict_evaluation), parameters of functions are evaluated before these functions are invoked. In [lazy programming languages](https://en.wikipedia.org/wiki/Evaluation_strategy#Non-strict_evaluation), the evaluation happens after invocation, if the formal parameters are effectively used. Languages are strict or lazy by default, sometimes providing developers with constructs to modify this expected evaluation semantics. In this case, it is up to the programmer to decide when to use either approach. The goal of this project is to move this task to the compiler by introducing the notion of "*lazification*" of function arguments: a code transformation technique that replaces strict with lazy evaluation of parameters whenever such modification is deemed profitable.

This transformation involves a static analysis to identify function calls that are candidates for lazification, plus a code extraction technique that generates [closures](https://en.wikipedia.org/wiki/Closure_(computer_programming)) to be lazily activated. Code extraction uses an adaptation of the classic program [slicing](https://en.wikipedia.org/wiki/Program_slicing) technique adjusted for the [static single assignment](https://en.wikipedia.org/wiki/Static_single-assignment_form) (SSA) representation. If lazification is guided by profiling information, then it can deliver speedups even on traditional benchmarks that are heavily optimized.

We have implemented lazification onto [LLVM](https://llvm.org/) 14.0, and have applied it onto hundreds of C/C++ programs from the LLVM test-suite and from [SPEC CPU2017](https://www.spec.org/cpu2017/). During this evaluation, we could observe statistically significant speedups over [clang](https://clang.llvm.org/) -O3 on some large programs, including a speedup of 11.1% on Prolang's `Bison` and a speedup of 4.6% on SPEC CPU2017's `perlbench`, which has more than 1.7 million LLVM instructions once compiled with clang -O3.

## Building

Lazification has been implemented as an LLVM pass. To build the pass, assume that you have the LLVM libraries installed at `~/llvm-project/build/lib/cmake/llvm`. In this case, do:

    cd wyvern # The directory where you've unpacked this repo.
    mkdir build
    cd build
    cmake ../ -DLLVM_DIR="~/llvm-project/build/lib/cmake/llvm"
    make -j2
    
Once you are done with `make`, you should have a folder called `passes` in your `build` directory. Check that you now have a library `libWyvern.so` there.

## Running

Once you compile our LLVM pass, you can load it in the LLVM [optimizer](https://llvm.org/docs/CommandGuide/opt.html). This repository contains a few examples of code that are likely to benefit from lazification in the `test` folder. For instance, check out the file `test_performance.c`, which contains the code that we shall use as an example further down. You can translate this file into LLVM bytecodes as follows:

    clang -S -c -emit-llvm -Xclang -disable-O0-optnone test_performance.c  -o test.ll

Then, once you obtain a file written in LLVM assembly (`test.ll`), you can lazify it using the optimizer. Notice that lazification requires some previous application of a few LLVM passes (`LLVM_SUPPORT`):

    LLVM_SUPPORT="-mem2reg -mergereturn -function-attrs -loop-simplify -lcssa"
    WYVERN_LIB="~/wyvern/build/passes/libWyvern.so"
    opt -load $WYVERN_LIB -S $LLVM_SUPPORT -enable-new-pm=0 -lazify-callsites \
      -stats test.ll -o test_lazyfied.ll
      
The above commands generate two files in your working folder: `test.ll` and `test_lazyfied.ll`. The first file is the original program, the second, the lazified code that we generate. To test them both, do:

    clang test.ll -O3 -o test.exe
    clang test_lazyfied.ll -O3 -o test_lazified.exe
    time ./test.exe 1000000000
    time ./test_lazified.exe 1000000000

## Lazification in One Example

Some programming languages let developers specify function arguments that could be evaluated lazily. The optimization implemented in this repository moves the task of recognizing profitable lazification opportunities to the compiler. In other words, our optimization:

1. Automatically  identifies opportunities for lazy evaluation, and
2. Transforms the code to capitalize on such opportunities.

To demonstrate its workings, we shall rely on Figure 1, which shows a situation where automatic lazification delivers a large benefit. The logical conjunction (&&) at Line 02 in Figure 1 (a) implements *short-circuit* semantics: if it is possible to resolve the logical expression by evaluating only the first term (`key != 0`), then the second term is not evaluated. Nevertheless, the symbols used in the conjunction, namely `key` and `value`, are fully evaluated before function `callee` is invoked at Line 15 of Figure 1 (a). This fact is unfortunate, because the computation of the second variable, `value`, involves a potentially heavy load of computation, comprising the code from Line 09 to Line 14 of Figure 1 (a).


![Figure 1: example of Lazification](/assets/images/ShortCircuitExample.png)

The computation of `value` in Figure 1 (a) is a  promising candidate for *lazification*. Figure 1 (b) shows the code that results from this optimization. For the sake of presentation only, we shall illustrate the effects of lazification using high-level C code. However, the prototype in this repository has been implemented onto LLVM, and affects exclusively the [intermediate representation](https://llvm.org/docs/LangRef.html) of this compiler. Lazification, as engineered in this repository, is implemented as a form of *function outlining*: part of the program code is extracted into a separate function (a closure) which can be invoked as needed. This *thunk* appears in Lines 01-08 of Figure 1 (b). The thunk is a triple formed by a table that binds values to free variables (Lines 03--05); a single-value cache (Lines 06 and 07) and a pointer to a function (Line 02). This function implements the computation to be performed lazily. This function appears in lines 26 to 40 of Figure 1 (b). An invocation to this closure in Line 10 of Figure 1 (b) replaces the use of formal parameter `value`, which was computed eagerly in the original program, seen in Figure 1 (a).

If the test `key != 0` is often false in Line 02 of Figure 1 (a), then lazy evaluation becomes attractive. In this case, the speedup that lazification delivers onto the program in Figure 1 (a) is linearly proportional to `N`. For instance, on a single-core x86 machine clocked at 2.2GHz, using a table with  one million entries (`N` = 1,000,000), and ten thousand input strings, the original program runs in 4.690s, whereas its lazy version in Figure 1 (b) runs in 0.060s. This difference increases with `N`.

The profitability of lazification depends on the program's dynamic behavior. If a function argument is rarely used, then it pays off to pass this argument as call-by-need. Otherwise, lazification leads to performance degradation. Regressions happen not only due to the cost of invoking the closure, but also to the fact that function outlining decreases the compiler's ability to carry out context-sensitive optimizations. Thus, to make lazification practical, we have made it profile-guided. Nevertheless, our current implementation of lazification is able to transform the program in Figure 1 (a) into the program in Figure 1 (b) in a completely automatic way: it requires no annotations or otherwise any other intervention from users.