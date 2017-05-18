#include <stdio.h>
#include <stdlib.h>

// 原始系统free释放方法
void zlibc_free(void *ptr)
{
	free(ptr);
}

#include <string.h>
#if defined(_WIN32)
#include <mutex>
#include <windows.h>
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

// __atomic_add_fetch是C++11特性中提供的原子加操作
#if defined(__ATOMIC_RELAXED)
#define update_zmalloc_stat_add(__n) __atomic_add_fetch(&used_memory, (__n), __ATOMIC_RELAXED)
#define update_zmalloc_stat_sub(__n) __atomic_sub_fetch(&used_memory, (__n), __ATOMIC_RELAXED)
// 如果不支持C++11，则调用GCC提供的原子加操作
#elif defined(HAVE_ATOMIC)
#define update_zmalloc_stat_add(__n) __sync_add_and_fetch(&used_memory, (__n))
#define update_zmalloc_stat_sub(__n) __sync_sub_and_fetch(&used_memory, (__n))
#else
#if defined(_WIN32)
#define update_zmalloc_stat_add(__n) InterlockedExchangeAdd(&used_memory, (__n))

#define update_zmalloc_stat_sub(__n) do {	\
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
	if (_n&(sizeof(long)-1)) _n += sizeof(long)-(_n&(sizeof(long)-1)); \
	if (zmalloc_thread_safe) { \
		update_zmalloc_stat_sub(_n); \
	} else { \
		used_memory -= _n; \
	} \
} while(0)

// 已使用的内存
static size_t used_memory = 0;
// 是否线程安全
static int zmalloc_thread_safe = 0;
#if defined(_WIN32)
std::mutex used_memory_mutex;
#else
pthread_mutex_t used_memory_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

// 默认内存溢出处理函数
static void zmalloc_default_oom(size_t size)
{
	fprintf(stderr, "zmalloc, Out of memory trying to allocate %zu bytes\n", size);
	fflush(stderr);
	abort();
}

//自定义异常处理函数
static void(*zmalloc_oom_handler)(size_t) = zmalloc_default_oom;

// 申请内存
void *zmalloc(size_t size)
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

void *zcalloc(size_t size)
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

// 调整已申请内存的大小
void *zrealloc(void *ptr, size_t size)
{
#ifndef HAVE_MALLOC_SIZE
	void *realptr;
#endif
	size_t oldsize;
	void *newptr;

	if (ptr == NULL) return zmalloc(size);
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

// 内存释放
void zfree(void *ptr)
{
#ifndef HAVE_MALLOC_SIZE
	size_t oldsize;
	void *realptr;
#endif

	if (ptr == NULL) return;
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


// 字符串拷贝
char *zstrdup(const char *s)
{
	size_t l = strlen(s) + 1;
	char *p = (char*)zmalloc(l);
	memcpy(p, s, l);
	return p;
}

// 获取当前以及占用的内存空间大小
size_t zmalloc_used_memory(void)
{
	size_t um;
	if (zmalloc_thread_safe)
	{
#if defined(__ATOMIC_RELAXED) || defined(HAVE_ATOMIC)
		um = update_zmalloc_stat_add(0);
#else
#if defined(_WIN32)
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

//设置线程安全模式
void zmalloc_enable_thread_safeness()
{
	zmalloc_thread_safe = 1;
}

// 自定义内存溢出的处理方法
void zmalloc_set_oom_handler(void(*oom_handler)(size_t))
{
	zmalloc_oom_handler = oom_handler;
}

// 获取所给内存和已使用内存的大小之比
float zmalloc_get_fragmentation_ratio(size_t rss)
{
	return (float)(rss / zmalloc_used_memory());
}

//获取RSS信息(Resident Set Size)
size_t zmalloc_get_rss(void)
{
#if defined(HAVE_PROC_STAT)
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
	int page = sysconf(_SC_PAGESIZE);
	size_t rss;
	char buf[4096];
	char filename[256];
	int fd, count;
	char *p, *x;

	snprintf(filename, 256, "/proc/%d/stat", getpid());
	if ((fd = open(filename, O_RDONLY)) == -1) return 0;
	if (read(fd, buf, 4096) <= 0) {
		close(fd);
		return 0;
	}
	close(fd);

	p = buf;
	count = 23; /* RSS is the 24th field in /proc/<pid>/stat */
	while (p && count--) {
		p = strchr(p, ' ');
		if (p) p++;
	}
	if (!p) return 0;
	x = strchr(p, ' ');
	if (!x) return 0;
	*x = '\0';

	rss = strtoll(p, NULL, 10);
	rss *= page;
	return rss;
#elif defined(HAVE_TASKINFO)
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <mach/task.h>
#include <mach/mach_init.h>
	task_t task = MACH_PORT_NULL;
	struct task_basic_info t_info;
	mach_msg_type_number_t t_info_count = TASK_BASIC_INFO_COUNT;

	if (task_for_pid(current_task(), getpid(), &task) != KERN_SUCCESS)
		return 0;
	task_info(task, TASK_BASIC_INFO, (task_info_t)&t_info, &t_info_count);

	return t_info.resident_size;
#else
	return zmalloc_used_memory();
#endif
}

// 从/proc/self/smap中获取指定字段的字节数
size_t zmalloc_get_smap_bytes_by_field(char *field)
{
#if defined(HAVE_PROC_SMAPS)
	char line[1024];
	size_t bytes = 0;
	FILE *fp = fopen("/proc/self/smaps", "r");
	int flen = strlen(field);

	if (!fp) return 0;
	while (fgets(line, sizeof(line), fp) != NULL)
	{
		if (strncmp(line, field, flen) == 0)
		{
			char *p = strchr(line, 'k');
			if (p) 
			{
				*p = '\0';
				bytes += strtol(line + flen, NULL, 10) * 1024;
			}
		}
	}
	fclose(fp);
	return bytes;
#else
	((void)field);
	return 0;
#endif
}

// 获得实际内存大小
size_t zmalloc_get_private_dirty(void)
{
	return zmalloc_get_smap_bytes_by_field("Private_Dirty:");
}

// 获取物理内存大小
size_t zmalloc_get_memory_size()
{
#if defined(_WIN32) && (defined(__CYGWIN__) || defined(__CYGWIN32__))
	/* Cygwin under Windows. ------------------------------------ */
	/* New 64-bit MEMORYSTATUSEX isn't available.  Use old 32.bit */
	MEMORYSTATUS status;
	status.dwLength = sizeof(status);
	GlobalMemoryStatus(&status);
	return (size_t)status.dwTotalPhys;
#elif defined(_WIN32)
	/* Windows. ------------------------------------------------- */
	/* Use new 64-bit MEMORYSTATUSEX, not old 32-bit MEMORYSTATUS */
	MEMORYSTATUSEX status;
	status.dwLength = sizeof(status);
	GlobalMemoryStatusEx(&status);
	return (size_t)status.ullTotalPhys;
#elif defined(__unix__) || defined(__unix) || defined(unix) || (defined(__APPLE__) && defined(__MACH__))
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