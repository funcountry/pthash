#pragma once

#include <cstdio>

// Define PTHASH_ENABLE_INSTRUMENTATION to 0 by default (disabled)
#ifndef PTHASH_ENABLE_INSTRUMENTATION
#define PTHASH_ENABLE_INSTRUMENTATION 0
#endif

// Conditional macros for instrumentation
#if PTHASH_ENABLE_INSTRUMENTATION
    #define PTHASH_LOG(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)
    #define PTHASH_LOG_VARS(x) x
    // These variables will only be defined when instrumentation is enabled
    #define PTHASH_LOG_VAR(type, name, value) type name = value
#else
    #define PTHASH_LOG(fmt, ...) ((void)0) // No-op
    #define PTHASH_LOG_VARS(x) ((void)0) // No-op for variables
    // In disabled mode, these will be eliminated by the compiler
    #define PTHASH_LOG_VAR(type, name, value) ((void)0)
#endif 