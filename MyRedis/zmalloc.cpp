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

// __atomic_add_fetch��C++11�������ṩ��ԭ�ӼӲ���
#if defined(__ATOMIC_RELAXED)
#define update_zmalloc_stat_add(__n) __atomic_add_fetch(&used_memory, (__n), __ATOMIC_RELAXED)
#define update_zmalloc_stat_sub(__n) __atomic_add_fetch(&used_memory, (__n), __ATOMIC_RELAXED)
// �����֧��C++11�������GCC�ṩ��ԭ�ӼӲ���
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

// ����used_memory��ֵ
#define update_zmalloc_stat_alloc(__n) do { \
	size_t _n = (__n); \
	if (_n&(sizeof(long)-1)) _n += sizeof(long)-(_n&(sizeof(long)-1)); /*��_n����Ϊsizeof(long)��������*/\
	if (zmalloc_thread_safe) { \
		update_zmalloc_stat_add(_n); /*����ԭ�Ӳ�����(+)�����������ڴ�*/ \
	} else { \
		used_memory += _n; \
	} \
} while(0)

#define update_zmalloc_stat_free(__n) do { \
	size_t _n = (__n); \
	if (_n&(sizeof(long)-1)) _n += sizeof(long)-(_n&(sizeof(long)-1)); \
	if (zmalloc_thread_safe) { \
		update_zmalloc_stat_sub(_n); \
	} else { \
		used_memory -= _n; \
	} \
} while(0)

static size_t used_memory = 0;
static int zmalloc_thread_safe = 0;
#if defined(_WIN32) || defined(_WIN64)
std::mutex used_memory_mutex;
#else
pthread_mutex_t used_memory_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

// Ĭ���ڴ����������
static void zmalloc_default_oom(size_t size)
{
	fprintf(stderr, "zmalloc, Out of memory trying to allocate %zu bytes\n", size);
	fflush(stderr);
	abort();
}

//�Զ����쳣������
static void(*zmalloc_oom_handler)(size_t) = zmalloc_default_oom;

// �����ڴ�
void* zmalloc(size_t size)
{
	// �������PREFIX_SIZE��С���ڴ����ڼ�¼�ö��ڴ�Ĵ�С
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

void* zcalloc(size_t size)
{
	void *ptr = calloc(1, size + PREFIX_SIZE);
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

// �����������ڴ�Ĵ�С
void* zrealloc(void *ptr, size_t size)
{
#ifndef HAVE_MALLOC_SIZE
	void *realptr;
#endif
	size_t oldsize;
	void *newptr;

	if (ptr == nullptr) return zmalloc(size);
#ifdef HAVE_MALLOC_SIZE
	oldsize = zmalloc_size(ptr);
	newptr = realloc(ptr, size);
	if (!newptr) zmalloc_oom_handler(size);

	update_zmalloc_stat_free(oldsize);
	update_zmalloc_stat_alloc(zmlloc_size(newptr));
	return newptr;
#else
	realptr = (char*)ptr - PREFIX_SIZE;
	oldsize = *((size_t*)realptr);
	newptr = realloc(ptr, size + PREFIX_SIZE);
	if (!newptr) zmalloc_oom_handler(size);

	*((size_t*)newptr) = size;
	update_zmalloc_stat_free(oldsize);
	update_zmalloc_stat_alloc(size);
	return newptr;
#endif
}

#ifdef HAVE_MALLOC_SIZE
size_t zmalloc_size(void *ptr)
{
	void *realptr = (char*)ptr - PREFIX_SIZE;
	size_t size = *((size_t*)realptr);
	if (size&(sizeof(long) - 1)) size += sizeof(long) - (size&(sizeof(long) - 1));
	return size + PREFIX_SIZE;
}
#endif

// �ڴ��ͷ�
void* zfree(void *ptr, size_t size)
{
#ifndef HAVE_MALLOC_SIZE
	size_t oldsize;
	void *realptr;
#endif

	if (ptr == nullptr) return nullptr;
#ifdef HAVE_MALLOC_SIZE
	update_zmalloc_stat_free(size);
	free(ptr);
#else
	realptr = (char*)ptr - PREFIX_SIZE;
	oldsize = *((size_t*)realptr);
	update_zmalloc_stat_free(oldsize + PREFIX_SIZE);
	free(realptr);
#endif
}

// ԭʼϵͳfree�ͷŷ���
void zlibc_free(void *ptr)
{
	free(ptr);
}

// �ַ�������
char* zstrdup(const char *s)
{
	size_t l = strlen(s) + 1;
	char *p = (char*)zmalloc(l);
	memcpy(p, s, l);
	return p;
}

// ��ȡ��ǰ�Լ�ռ�õ��ڴ�ռ��С
size_t zmalloc_used_memory(void)
{
	size_t um;
	if (zmalloc_thread_safe)
	{
#if defined(__ATOMIC_RELAXED) || defined(HAVE_ATOMIC)
		um = update_zmalloc_stat_add(0);
#else
#if defined(_WIN32) || defined(_WIN64)
		std::lock_guard<std::mutex> lock(used_memory_mutex);
		um = used_memory;
#else
		pthread_mutex_lock(used_memory_mutex);
		um = used_memory;
		pthread_mutex_unlock(used_memory_mutex);
#endif
#endif
	}
	else
	{
		um = used_memory;
	}

	return um;
}

//�����̰߳�ȫģʽ
void zmalloc_enable_thread_safeness()
{
	zmalloc_thread_safe = 1;
}

// �Զ����ڴ�����Ĵ�����
void zmalloc_set_oom_handler(void(*oom_handler)(size_t))
{
	zmalloc_oom_handler = oom_handler;
}

// ��ȡ�����ڴ��С
size_t zmalloc_get_memory_size()
{
#if defined(__unix__) || defined(__unix) || defined(unix) || \
    (defined(__APPLE__) && defined(__MACH__))
#if defined(CTL_HW) && (defined(HW_MEMSIZE) || defined(HW_PHYSMEM64))
	int mid[2];
	mid[0] = CTL_HW;
#if defined(HW_MEMSIZE)
	mib[1] = HW_MEMSIZE;		/* OSX. --------------------- */
#elif defined(HW_PHYSMEM64)
	mib[1] = HW_PHYSMEM64;		/* NetBSD, OpenBSD. --------- */
#endif
	int64_t size = 0;
	size_t len = sizeof(size);
	if (sysctl(mib, 2, &size, &len, NULL, 0) == 0)
		return (size_t)size;
	return 0L;
#elif defined(_SC_PHYS_PAGES) && defined(_SC_PAGESIZE)
	/* FreeBSD, Linux, OpenBSD, and Solaris. -------------------- */
	return (size_t)sysconf(_SC_PHYS_PAGES) * (size_t)sysconf(_SC_PAGESIZE);

#elif defined(CTL_HW) && (defined(HW_PHYSMEM) || defined(HW_REALMEM))
	/* DragonFly BSD, FreeBSD, NetBSD, OpenBSD, and OSX. -------- */
	int mib[2];
	mib[0] = CTL_HW;
#if defined(HW_REALMEM)
	mib[1] = HW_REALMEM;        /* FreeBSD. ----------------- */
#elif defined(HW_PYSMEM)
	mib[1] = HW_PHYSMEM;        /* Others. ------------------ */
#endif
	unsigned int size = 0;      /* 32-bit */
	size_t len = sizeof(size);
	if (sysctl(mib, 2, &size, &len, NULL, 0) == 0)
		return (size_t)size;
	return 0L;          /* Failed? */
#endif /* sysctl and sysconf variants */

#else
	return 0L;          /* Unknown OS. */
#endif
}