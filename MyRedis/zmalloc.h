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
#endif
