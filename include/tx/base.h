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

#define BYTE_WIDTH __UINT8_WIDTH__
#define U8_WIDTH __UINT8_WIDTH__
#define U16_WIDTH __UINT16_WIDTH__
#define U32_WIDTH __UINT32_WIDTH__
#define U64_WIDTH __UINT64_WIDTH__
#define I8_WIDTH __INT8_WIDTH__
#define I16_WIDTH __INT16_WIDTH__
#define I32_WIDTH __INT32_WIDTH__
#define I64_WIDTH __INT64_WIDTH__
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

#endif // __TX_BASE_H__
