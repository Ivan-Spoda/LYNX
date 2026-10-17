/* LYNX: hand-written configuration for building libmp3lame without autoconf (encoder only, no mpglib decoder,
   no assembly or SSE paths). Not part of the LAME distribution. */
#ifndef LYNX_LAME_CONFIG_H
#define LYNX_LAME_CONFIG_H
#define STDC_HEADERS 1
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_LIMITS_H 1
#define HAVE_ERRNO_H 1
#define HAVE_FCNTL_H 1
#define HAVE_STRCHR 1
#define HAVE_MEMCPY 1
#define HAVE_STRING_H 1
#define HAVE_STDLIB_H 1
#define USE_FAST_LOG 1
#define TAKEHIRO_IEEE754_HACK 1
typedef float ieee754_float32_t;
#endif
