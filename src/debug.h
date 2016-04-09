#ifndef __DEBUG_H__
#define __DEBUG_H__

#include <stdio.h>

#ifndef NDEBUG
#define debug_info(fmt, ...)	printf(fmt, ## __VA_ARGS__)
#else
#define debug_info(fmt, ...)
#endif

#endif