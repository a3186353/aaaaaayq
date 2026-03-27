/*
 * hconfig.h — iOS 手动配置 (替代 libhv CMake 自动生成)
 *
 * iOS (Darwin/ARM64) 平台特性标记
 */
#ifndef HV_CONFIG_H_
#define HV_CONFIG_H_

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

/* SSL 由 ghv_crypto 直接使用 OpenSSL，不走 libhv SSL 层 */
/* #undef WITH_OPENSSL */
/* #undef WITH_GNUTLS */
/* #undef WITH_MBEDTLS */

/* iOS 使用 kqueue */
/* #undef WITH_WEPOLL */
/* #undef WITH_KCP */
/* #undef WITH_IO_URING */

#endif /* HV_CONFIG_H_ */
