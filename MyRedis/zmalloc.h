#ifndef __ZMALLOC_H
#define __ZMALLOC_H

#if defined(USE_TCMALLOC)
#include <google/tcmalloc.h>
#elif defined(USE_JEMALLOC)
#include <jemalloc/jemalloc.h>
#elif defined(__APPLE__)
#include <malloc/malloc.h>
#endif

void* zmalloc(size_t size);
void* zcalloc(size_t size);
void* zrealloc(void *ptr, size_t size);
void zfree(void *ptr, size_t size);
char* zstrdup(const char *s);
size_t zmalloc_used_memory(void);
void zmalloc_enable_thread_safeness(void);
void zmalloc_set_oom_handler(void(*oom_handler)(size_t));
float zmalloc_get_fragmentation_ratio(size_t rss);
size_t zmalloc_get_rss(void);
size_t zmalloc_get_private_dirty(void);
size_t zmalloc_get_smap_bytes_by_field(char *field);
size_t zmalloc_get_memory_size(void);
void zlibc_free(void *ptr);

#ifdef HAVE_MALLOC_SIZE
size_t	zmalloc_size(void *ptr);
#endif

#endif
