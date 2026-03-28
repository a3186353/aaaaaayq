/*
 * hconfig.h — 跨平台手动配置 (替代 libhv CMake 自动生成)
 *
 * 根据 hconfig.h.in 模板，为各平台填入正确的特性宏。
 * 此文件由所有平台共享 (Dependencies/ 是唯一源头)。
 */
#ifndef HV_CONFIG_H_
#define HV_CONFIG_H_

/* ============================================================
 * 平台检测
 * ============================================================ */

#if defined(_WIN32) || defined(_WIN64)
  /* ---- Windows (MSVC / MinGW) ---- */

  #ifndef HAVE_STDBOOL_H
  #define HAVE_STDBOOL_H  1
  #endif

  #ifndef HAVE_STDINT_H
  #define HAVE_STDINT_H   1
  #endif

  #ifndef HAVE_STDATOMIC_H
  #define HAVE_STDATOMIC_H 0  /* MSVC C mode 不支持 <stdatomic.h> */
  #endif

  #ifndef HAVE_SYS_TYPES_H
  #define HAVE_SYS_TYPES_H 1
  #endif

  #ifndef HAVE_SYS_STAT_H
  #define HAVE_SYS_STAT_H 1
  #endif

  #ifndef HAVE_SYS_TIME_H
  #define HAVE_SYS_TIME_H 0  /* Windows 没有 <sys/time.h> */
  #endif

  #ifndef HAVE_FCNTL_H
  #define HAVE_FCNTL_H    0  /* Windows 没有 <fcntl.h> 的 POSIX 子集 */
  #endif

  #ifndef HAVE_PTHREAD_H
  #define HAVE_PTHREAD_H  0  /* Windows 用 Win32 线程 */
  #endif

  #ifndef HAVE_ENDIAN_H
  #define HAVE_ENDIAN_H   0
  #endif

  #ifndef HAVE_SYS_ENDIAN_H
  #define HAVE_SYS_ENDIAN_H 0
  #endif

  #ifndef HAVE_GETTID
  #define HAVE_GETTID     0
  #endif

  #ifndef HAVE_STRLCPY
  #define HAVE_STRLCPY    0
  #endif

  #ifndef HAVE_STRLCAT
  #define HAVE_STRLCAT    0
  #endif

  #ifndef HAVE_CLOCK_GETTIME
  #define HAVE_CLOCK_GETTIME 0  /* Windows 没有 clock_gettime */
  #endif

  #ifndef HAVE_GETTIMEOFDAY
  #define HAVE_GETTIMEOFDAY 0  /* Windows 没有 gettimeofday */
  #endif

  #ifndef HAVE_PTHREAD_SPIN_LOCK
  #define HAVE_PTHREAD_SPIN_LOCK 0
  #endif

  #ifndef HAVE_PTHREAD_MUTEX_TIMEDLOCK
  #define HAVE_PTHREAD_MUTEX_TIMEDLOCK 0
  #endif

  #ifndef HAVE_SEM_TIMEDWAIT
  #define HAVE_SEM_TIMEDWAIT 0
  #endif

  #ifndef HAVE_PIPE
  #define HAVE_PIPE       0  /* Windows 用 _pipe (CRT) */
  #endif

  #ifndef HAVE_SOCKETPAIR
  #define HAVE_SOCKETPAIR 0  /* Windows 没有 POSIX socketpair */
  #endif

  #ifndef HAVE_EVENTFD
  #define HAVE_EVENTFD    0  /* 仅 Linux */
  #endif

  #ifndef HAVE_SETPROCTITLE
  #define HAVE_SETPROCTITLE 0
  #endif

  /* SSL 由 ghv_crypto 直接使用 OpenSSL，不走 libhv SSL 层 */
  /* Windows IO: IOCP (通过 vcxproj 定义 EVENT_IOCP) */

#elif defined(__APPLE__)
  /* ---- iOS / macOS (Apple Clang) ---- */

  #ifndef HAVE_STDBOOL_H
  #define HAVE_STDBOOL_H  1
  #endif

  #ifndef HAVE_STDINT_H
  #define HAVE_STDINT_H   1
  #endif

  #ifndef HAVE_STDATOMIC_H
  #define HAVE_STDATOMIC_H 1
  #endif

  #ifndef HAVE_SYS_TYPES_H
  #define HAVE_SYS_TYPES_H 1
  #endif

  #ifndef HAVE_SYS_STAT_H
  #define HAVE_SYS_STAT_H 1
  #endif

  #ifndef HAVE_SYS_TIME_H
  #define HAVE_SYS_TIME_H 1
  #endif

  #ifndef HAVE_FCNTL_H
  #define HAVE_FCNTL_H    1
  #endif

  #ifndef HAVE_PTHREAD_H
  #define HAVE_PTHREAD_H  1
  #endif

  #ifndef HAVE_ENDIAN_H
  #define HAVE_ENDIAN_H   0  /* Darwin 没有 <endian.h> */
  #endif

  #ifndef HAVE_SYS_ENDIAN_H
  #define HAVE_SYS_ENDIAN_H 0
  #endif

  #ifndef HAVE_GETTID
  #define HAVE_GETTID     0  /* iOS 没有 gettid() */
  #endif

  #ifndef HAVE_STRLCPY
  #define HAVE_STRLCPY    1
  #endif

  #ifndef HAVE_STRLCAT
  #define HAVE_STRLCAT    1
  #endif

  #ifndef HAVE_CLOCK_GETTIME
  #define HAVE_CLOCK_GETTIME 1
  #endif

  #ifndef HAVE_GETTIMEOFDAY
  #define HAVE_GETTIMEOFDAY 1
  #endif

  #ifndef HAVE_PTHREAD_SPIN_LOCK
  #define HAVE_PTHREAD_SPIN_LOCK 0  /* Darwin 不支持 pthread_spinlock */
  #endif

  #ifndef HAVE_PTHREAD_MUTEX_TIMEDLOCK
  #define HAVE_PTHREAD_MUTEX_TIMEDLOCK 0  /* Darwin 不支持 */
  #endif

  #ifndef HAVE_SEM_TIMEDWAIT
  #define HAVE_SEM_TIMEDWAIT 0  /* Darwin 不支持 */
  #endif

  #ifndef HAVE_PIPE
  #define HAVE_PIPE       1
  #endif

  #ifndef HAVE_SOCKETPAIR
  #define HAVE_SOCKETPAIR 1
  #endif

  #ifndef HAVE_EVENTFD
  #define HAVE_EVENTFD    0  /* 仅 Linux */
  #endif

  #ifndef HAVE_SETPROCTITLE
  #define HAVE_SETPROCTITLE 0
  #endif

  /* iOS 使用 kqueue (通过 CMake/vcxproj 定义 EVENT_KQUEUE) */

#elif defined(__ANDROID__) || defined(__linux__)
  /* ---- Android / Linux (Clang/GCC) ---- */

  #ifndef HAVE_STDBOOL_H
  #define HAVE_STDBOOL_H  1
  #endif

  #ifndef HAVE_STDINT_H
  #define HAVE_STDINT_H   1
  #endif

  #ifndef HAVE_STDATOMIC_H
  #define HAVE_STDATOMIC_H 1
  #endif

  #ifndef HAVE_SYS_TYPES_H
  #define HAVE_SYS_TYPES_H 1
  #endif

  #ifndef HAVE_SYS_STAT_H
  #define HAVE_SYS_STAT_H 1
  #endif

  #ifndef HAVE_SYS_TIME_H
  #define HAVE_SYS_TIME_H 1
  #endif

  #ifndef HAVE_FCNTL_H
  #define HAVE_FCNTL_H    1
  #endif

  #ifndef HAVE_PTHREAD_H
  #define HAVE_PTHREAD_H  1
  #endif

  #ifndef HAVE_ENDIAN_H
  #define HAVE_ENDIAN_H   1
  #endif

  #ifndef HAVE_SYS_ENDIAN_H
  #define HAVE_SYS_ENDIAN_H 0
  #endif

  #ifndef HAVE_GETTID
  #define HAVE_GETTID     1
  #endif

  #ifndef HAVE_STRLCPY
  #define HAVE_STRLCPY    0
  #endif

  #ifndef HAVE_STRLCAT
  #define HAVE_STRLCAT    0
  #endif

  #ifndef HAVE_CLOCK_GETTIME
  #define HAVE_CLOCK_GETTIME 1
  #endif

  #ifndef HAVE_GETTIMEOFDAY
  #define HAVE_GETTIMEOFDAY 1
  #endif

  #ifndef HAVE_PTHREAD_SPIN_LOCK
  #define HAVE_PTHREAD_SPIN_LOCK 1
  #endif

  #ifndef HAVE_PTHREAD_MUTEX_TIMEDLOCK
  #define HAVE_PTHREAD_MUTEX_TIMEDLOCK 1
  #endif

  #ifndef HAVE_SEM_TIMEDWAIT
  #define HAVE_SEM_TIMEDWAIT 1
  #endif

  #ifndef HAVE_PIPE
  #define HAVE_PIPE       1
  #endif

  #ifndef HAVE_SOCKETPAIR
  #define HAVE_SOCKETPAIR 1
  #endif

  #ifndef HAVE_EVENTFD
  #define HAVE_EVENTFD    1
  #endif

  #ifndef HAVE_SETPROCTITLE
  #define HAVE_SETPROCTITLE 0
  #endif

  /* Android/Linux 使用 epoll (通过 vcxproj 定义 EVENT_EPOLL) */

#else
  #error "hconfig.h: unsupported platform"
#endif

#endif /* HV_CONFIG_H_ */
