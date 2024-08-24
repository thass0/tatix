#ifndef __TX_BASE_H__
#define __TX_BASE_H__

#ifndef __GNUC__
#error "We use GCC here, go away with your strange, non-GCC compiler"
#endif

#ifndef __x86_64__
#error "This kernel is written for the x86_64 architecture"
#endif

////////////////////////////////////////////////////////////////////////////////
// Base types                                                                 //
////////////////////////////////////////////////////////////////////////////////

// We make extensive use of GCC predefined macros here:
// https://gcc.gnu.org/onlinedocs/cpp/Common-Predefined-Macros.html

typedef unsigned char byte;
typedef __UINT8_TYPE__ u8;
typedef __UINT16_TYPE__ u16;
typedef __UINT32_TYPE__ u32;
typedef __UINT64_TYPE__ u64;
typedef __INT8_TYPE__ i8;
typedef __INT16_TYPE__ i16;
typedef __INT32_TYPE__ i32;
typedef __INT64_TYPE__ i64;
// We only do signed pointer and size types
typedef __INTPTR_TYPE__ ptr;
typedef __UINTPTR_TYPE__ uptr;
typedef __PTRDIFF_TYPE__ sz;
typedef __SIZE_TYPE__ usz;
typedef u8 bool;

#if __SIZEOF_FLOAT__ == 4
typedef float f32;
#else // __SIZEOF_FLOAT__ != 4
#error "Expected float to by four bytes wide"
#endif // __SIZEOF_FLOAT__ != 4

#if __SIZEOF_DOUBLE__ == 8
typedef double f64;
#else // __SIZEOF_DOUBLE__ != 8
#error "Expected double eto be eight bytes wide"
#endif // __SIZEOF_DOUBLE__ != 8

////////////////////////////////////////////////////////////////////////////////
// Limits                                                                     //
////////////////////////////////////////////////////////////////////////////////

#define BYTE_MAX __UINT8_MAX__
#define U8_MAX __UINT8_MAX__
#define U16_MAX __UINT16_MAX__
#define U32_MAX __UINT32_MAX__
#define U64_MAX __UINT64_MAX__
#define I8_MAX __INT8_MAX__
#define I16_MAX __INT16_MAX__
#define I32_MAX __INT32_MAX__
#define I64_MAX __INT64_MAX__
#define PTR_MAX __INTPTR_MAX__
#define UPTR_MAX __UINTPTR_MAX__
#define SZ_MAX __PTRDIFF_MAX__
#define USZ_MAX __SIZE_MAX__

#define BYTE_WIDTH 8
#define U8_WIDTH 8
#define U16_WIDTH 16
#define U32_WIDTH 32
#define U64_WIDTH 64
#define I8_WIDTH 8
#define I16_WIDTH 16
#define I32_WIDTH 32
#define I64_WIDTH 64
#define PTR_WIDTH __INTPTR_WIDTH__
#define UPTR_WIDTH __UINTPTR_WIDTH__
#define SZ_WIDTH __PTRDIFF_WIDTH__
#define USZ_WIDTH __SIZE_WIDTH__

////////////////////////////////////////////////////////////////////////////////
// Fundamental macros                                                         //
////////////////////////////////////////////////////////////////////////////////

// Make sure to enable -Wno-keyword-macro so this doesn't annoy you
#define sizeof(x) ((sz)sizeof(x))
#define countof(arr) (sizeof(arr) / sizeof((arr)[0]))
#define lengthof(str) (countof(str) - 1)
#define alignof(x) ((sz) __alignof__(x))
#define offsetof(type, member) __builtin_offsetof(type, member)
#define NULL ((void *)0)
#define true 1
#define false 0
#define STRINGIFY(x) #x
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define BIT(n) (1LLU << (n))

////////////////////////////////////////////////////////////////////////////////
// Variadic functions                                                         //
////////////////////////////////////////////////////////////////////////////////

#define va_start(v, l) __builtin_va_start(v, l)
#define va_end(v) __builtin_va_end(v)
#define va_arg(v, l) __builtin_va_arg(v, l)
#define va_copy(d, s) __builtin_va_copy(d, s)
typedef __builtin_va_list va_list;

////////////////////////////////////////////////////////////////////////////////
// Attributes                                                                 //
////////////////////////////////////////////////////////////////////////////////

#define __packed __attribute__((packed))
#define __aligned(n) __attribute__((aligned(n)))
#define __no_caller_saved_regs __attribute__((no_caller_saved_registers))
#define __unused __attribute__((unused))
#define __used __attribute__((used))
#define __section(s) __attribute__((section(s)))
#define __noreturn __attribute__((noreturn))
#define __naked __attribute__((naked))
#define __container_of(x, type, member) ((type *)((const volatile byte *)(x) - offsetof(type, member)))

#endif // __TX_BASE_H__
