/*
 * mem.c - the two routines GCC may emit calls to in a freestanding build
 * (struct copies and zero-initialisation). No C library is linked.
 */
#include <stddef.h>
#include <stdint.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int value, size_t n);

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    size_t i;

    for (i = 0U; i < n; i++)
    {
        d[i] = s[i];
    }
    return dst;
}

void *memset(void *dst, int value, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    size_t i;

    for (i = 0U; i < n; i++)
    {
        d[i] = (uint8_t)value;
    }
    return dst;
}
