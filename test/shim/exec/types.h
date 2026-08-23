/* Minimale exec/types.h zodat de platform-onafhankelijke onderdelen
   (parse.c, url.c) ook op een moderne host te compileren en te testen zijn. */
#ifndef EXEC_TYPES_H
#define EXEC_TYPES_H
#include <stdint.h>
typedef int32_t  LONG;
typedef uint32_t ULONG;
typedef int16_t  WORD;
typedef uint16_t UWORD;
typedef uint8_t  UBYTE;
typedef int8_t   BYTE;
typedef void    *APTR;
typedef char    *STRPTR;
typedef int32_t  BOOL;
#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif
#endif
