#ifdef _WIN32
#include <windows.h>
#include <immintrin.h>
#elif defined(__APPLE__)
    #include <pthread.h>
#else
    #define _GNU_SOURCE
    #include <sched.h>
    #include <pthread.h>
#endif


    static void spin_pause() noexcept {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
        _mm_pause();
#elif defined(__aarch64__) || defined(_M_ARM64)
        asm volatile("yield" ::: "memory");
#else
        std::this_thread::yield();
#endif
}
