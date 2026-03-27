/*
 * iOS compatibility shim for <sys/random.h>
 *
 * iOS SDK does not ship <sys/random.h>, but SQLite3MultipleCiphers'
 * chacha20poly1305.c conditionally includes it when __MAC_10_12 is defined
 * (which Apple SDK headers define even in iOS builds).
 *
 * On iOS, getentropy() is available via <unistd.h> (iOS 10+), and the
 * actual code path in chacha20poly1305.c falls through to read_urandom()
 * anyway (since __MAC_OS_X_VERSION_MAX_ALLOWED is not set for iOS).
 * This empty shim just prevents the #include from failing.
 */
#ifndef _IOS_COMPAT_SYS_RANDOM_H
#define _IOS_COMPAT_SYS_RANDOM_H

/* Empty — getentropy() prototype is already in <unistd.h> on iOS 10+ */

#endif /* _IOS_COMPAT_SYS_RANDOM_H */
