#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32) || defined(_WIN64)
#include <mutex>
#else
#include <pthread.h>
#endif
#include "zmalloc.h"

#ifdef HAVE_MALLOC_SIZE
#define PREFIX_SIZE (0)
#else
#if defined(__sun) || defined(__sparc) || defined(__sparc__)
#define PREFIX_SIZE (sizeof(long long))
#else
#define PREFIX_SIZE (sizeof(size_t))
#endif
#endif

#if defined(__ATOMIC_RELAXED)
#define update_zmalloc_stat_add(__n) __atomic_add_fetch(&used_memory, (__n), __ATOMIC_RELAXED)
#define update_zmalloc_stat_sub(__n) __atomic_add_fetch(&used_memory, (__n), __ATOMIC_RELAXED)
#elif defined(HAVE_ATOMIC)
#define update_zmalloc_stat_add(__n) __sync_add_and_fetch(&used_memory, (__n))
#define update_zmalloc_stat_sub(__n) __sync_add_and_fetch(&used_memory, (__n))
#else
#if defined(_WIN32) || defined(_WIN64)
#define update_zmalloc_stat_add(__n) do { \
	std::lock_guard<std::mutex> lock(used_memory_mutex); \
	used_memory += (__n); \
} while (0)

#define update_zmalloc_stat_sub(__n) do { \
	std::lock_guard<std::mutex> lock(used_memory_mutex); \
	used_memory -= (__n); \
} while (0)
#else
#define update_zmalloc_stat_add(__n) do { \
	pthread_mutex_lock(used_memory_mutex); \
	used_memory += (__n); \
	pthread_mutex_unlock(used_memory_mutex); \
} while (0)

#define update_zmalloc_stat_sub(__n) do { \
	pthread_mutex_lock(used_memory_mutex); \
	used_memory -= (__n); \
	pthread_mutex_unlock(used_memory_mutex); \
} while (0)
#endif

#endif

// 更新used_memory的值
#define update_zmalloc_stat_alloc(__n) do { \
	size_t _n = (__n); \
	if (_n&(sizeof(long)-1)) _n += sizeof(long)-(_n&(sizeof(long)-1)); /*将_n调整为sizeof(long)的整数倍*/\
	if (zmalloc_thread_safe) { \
		update_zmalloc_stat_add(_n); /*调用原子操作加(+)来更新已用内存*/ \
	} else { \
		used_memory += _n; \
	} \
} while(0)

#define update_zmalloc_stat_free(__n) do { \
	size_t _n = (__n); \
	if (_n&(sizeof(long)-1)) _n += sizeof(long)-(_n&sizeof(long)-1)); \
	if (zmalloc_thread_safe) { \
		update_zmalloc_stat_sub(_n); \
	} else { \
		used_memory -= _n; \
	} \
} while (0)

static size_t used_memory = 0;
static int zmalloc_thread_safe = 0;
#if defined(_WIN32) || defined(_WIN64)
std::mutex used_memory_mutex;
#else
pthread_mutex_t used_memory_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static void zmalloc_default_oom(size_t size)
{
	fprintf(stderr, "zmalloc, Out of memory trying to allocate %zu bytes\n", size);
	fflush(stderr);
	abort();
}

static void(*zmalloc_oom_handler)(size_t) = zmalloc_default_oom;

// 申请内存
void* zmalloc(size_t size)
{
	// 多申请的PREFIX_SIZE大小的内存用于记录该段内存的大小
	void *ptr = malloc(size + PREFIX_SIZE);
	if (!ptr) zmalloc_oom_handler(size);
#ifdef HAVE_MALLOC_SIZE
	update_zmalloc_stat_alloc(zmalloc_size(ptr));
	return ptr;
#else
	*((size_t*)ptr) = size;
	update_zmalloc_stat_alloc(size + PREFIX_SIZE);
	return (char*)ptr + PREFIX_SIZE;
#endif // HAVE_MALLOC_SIZE
}