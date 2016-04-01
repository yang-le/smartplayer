#ifndef __DEBUG_H__
#define __DEBUG_H__

#include <stdio.h>

#define debug_info(fmt, ...)	printf(fmt, ## __VA_ARGS__)

#endif