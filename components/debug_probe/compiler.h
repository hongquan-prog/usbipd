/*
 * Compiler Abstraction
 *
 * 标准GCC编译器宏定义，跨平台兼容
 */

#pragma once

#ifndef __STATIC_FORCEINLINE
#define __STATIC_FORCEINLINE    static inline __attribute__((always_inline))
#endif

#ifndef __STATIC_INLINE
#define __STATIC_INLINE         static inline __attribute__((always_inline))
#endif

#ifndef __WEAK
#define __WEAK                  __attribute__((weak))
#endif

#ifndef __NOP
#define __NOP()                 __asm volatile ("nop")
#endif

/* 兼容ARM CMSIS宏 */
#ifndef __STATIC_FORCEINLINE
#define __STATIC_FORCEINLINE    static inline __attribute__((always_inline))
#endif