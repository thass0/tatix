#ifndef _BASE_H_
#define _BASE_H_

#ifndef __GNUC__
#error "We use GCC here, go away with your strange, non-GCC compiler"
#endif

#define NULL ((void*)0)

// Use predefined GCC macros to get the types for the correct sizes
// (see https://gcc.gnu.org/onlinedocs/cpp/Common-Predefined-Macros.html).
typedef __INT8_TYPE__ i8;
typedef __INT16_TYPE__ i16;
typedef __INT32_TYPE__ i32;
typedef __INT64_TYPE__ i64;
typedef __UINT8_TYPE__ u8;
typedef __UINT16_TYPE__ u16;
typedef __UINT32_TYPE__ u32;
typedef __UINT64_TYPE__ u64;
// We only do signed pointer and size types
typedef __INTPTR_TYPE__ ptr;
typedef __PTRDIFF_TYPE__ size;
typedef float f32;
typedef double f64;
typedef u8 bool;

#define true 1
#define false 0

#endif // _BASE_H_
