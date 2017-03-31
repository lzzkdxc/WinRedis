#ifndef __SDS_H
#define __SDS_H

#define SDS_MAX_PREALLOC (1024*1024)

#include "stdint.h"

#pragma warning(disable:4267)
#pragma warning(disable:4244)
/*
——————————————————————————————————
|-----header----|------data------|
----------------------------------
|len|alloc|flags|r|e|d|i|s|\0|...|
——————————————————————————————————
*/

typedef char* sds;

#pragma pack(push, 1) //把原来对齐方式设置压栈，并设新的对齐方式设置为一个字节对齐
// sdshdr5 已不再使用
typedef struct _sdshdr5
{
	unsigned char flags;
	char buf[1];
}sdshdr5;

typedef struct _sdshdr8
{
	uint8_t len;			//表示字符串真正的长度，不包含空终止字符
	uint8_t alloc;			//表示字符串的最大容量，不包含Header和最后的空终止字符
	unsigned char flags;	//表示header的类型
	char buf[1];
}sdshdr8;

typedef struct _sdshdr16
{
	uint16_t len;
	uint16_t alloc;
	unsigned char flags;
	char buf[1];
}sdshdr16;

typedef struct _sdshdr32
{
	uint32_t len;
	uint32_t alloc;
	unsigned char flags;
	char buf[1];
}sdshdr32;

typedef struct _sdshdr64
{
	uint64_t len;
	uint64_t alloc;
	unsigned char flags;
	char buf[1];
}sdshdr64;

#pragma pack(pop)	// 恢复对齐状态

//5种header类型
#define SDS_TYPE_5	0
#define SDS_TYPE_8	1
#define SDS_TYPE_16 2
#define SDS_TYPE_32 3
#define SDS_TYPE_64 4
#define SDS_TYPE_MASK 7		//类型掩码
#define SDS_TYPE_BITS 3
//##是将两个符号连接成一个，如sdshdr和8（T为8）合成sdshdr8
#define SDS_HDR_VAR(T, s) sdshdr##T *sh = (sdshdr##T*)((s) - (sizeof(sdshdr##T))); // 获取header头指针
#define SDS_HDR(T, s) ((sdshdr##T *)((s) - (sizeof(sdshdr##T)))) // 获取header头指针
#define SDS_TYPE_5_LEN(f) ((f) >> SDS_TYPE_BITS) // 获取sdshdr5的长度

static inline size_t sdslen(const sds s)
{
	unsigned char flags = s[-1];
	switch (flags & SDS_TYPE_MASK)
	{
		case SDS_TYPE_5:
			return SDS_TYPE_5_LEN(flags);
		case SDS_TYPE_8:
			return SDS_HDR(8, s)->len;
		case SDS_TYPE_16:
			return SDS_HDR(16, s)->len;
		case SDS_TYPE_32:
			return SDS_HDR(32, s)->len;
		case SDS_TYPE_64:
			return SDS_HDR(64, s)->len;
	}
	return 0;
}

// 剩余空间
static inline size_t sdsavail(const sds s)
{
	unsigned char flags = s[-1];
	switch (flags & SDS_TYPE_MASK)
	{
		case SDS_TYPE_5:
			return 0;
		case SDS_TYPE_8:
		{
			SDS_HDR_VAR(8, s);
			return sh->alloc - sh->len;
		}
		case SDS_TYPE_16:
		{
			SDS_HDR_VAR(16, s);
			return sh->alloc - sh->len;
		}
		case SDS_TYPE_32:
		{
			SDS_HDR_VAR(32, s);
			return sh->alloc - sh->len;
		}
		case SDS_TYPE_64:
		{
			SDS_HDR_VAR(64, s);
			return sh->alloc - sh->len;
		}
	}
	return 0;
}

// 更新sds的长度
static inline void sdssetlen(sds s, size_t newlen)
{
	unsigned char flags = s[-1];
	switch (flags & SDS_TYPE_MASK)
	{
		case SDS_TYPE_5:
		{
			// 应该不会进入这里
			unsigned char *fp = ((unsigned char*)s) - 1;
			*fp = SDS_TYPE_5 | (newlen << SDS_TYPE_BITS);
			break;
		}
		case SDS_TYPE_8:
			SDS_HDR(8, s)->len = newlen;
			break;
		case SDS_TYPE_16:
			SDS_HDR(16, s)->len = newlen;
			break;
		case SDS_TYPE_32:
			SDS_HDR(32, s)->len = newlen;
			break;
		case SDS_TYPE_64:
			SDS_HDR(64, s)->len = newlen;
			break;
	}
}

// 获取sds的容量 sdsalloc() = sdsavail() + sdslen()
static inline size_t sdsalloc(const sds s)
{
	unsigned char flags = s[-1];
	switch (flags&SDS_TYPE_MASK)
	{
		case SDS_TYPE_5:
			return SDS_TYPE_5_LEN(flags);
		case SDS_TYPE_8:
			return SDS_HDR(8, s)->alloc;
		case SDS_TYPE_16:
			return SDS_HDR(16, s)->alloc;
		case SDS_TYPE_32:
			return SDS_HDR(32, s)->alloc;
		case SDS_TYPE_64:
			return SDS_HDR(64, s)->alloc;
	}
	return 0;
}

// 更新sds的容量
static inline void sdssetalloc(sds s, size_t newlen)
{
	unsigned char flags = s[-1];
	switch (flags&SDS_TYPE_MASK)
	{
		case SDS_TYPE_5:
			break;
		case SDS_TYPE_8:
			SDS_HDR(8, s)->alloc = newlen;
			break;
		case SDS_TYPE_16:
			SDS_HDR(16, s)->alloc = newlen;
			break;
		case SDS_TYPE_32:
			SDS_HDR(32, s)->alloc = newlen;
			break;
		case SDS_TYPE_64:
			SDS_HDR(64, s)->alloc = newlen;
			break;
	}
}

sds sdsnewlen(const void *init, size_t initlen);
sds sdsnew(const char *init);
sds sdsempty();
sds sdsfromlonglong(long long value);
sds sdsdup(const sds s);
void sdsfree(sds s);
sds sdsgrowzero(sds s, size_t len);
sds sdscatlen(sds s, const void *t, size_t len);
sds sdscat(sds s, const char *t);
sds sdscatsds(sds s, const sds t);
sds sdscpylen(sds s, const char *t, size_t len);
sds sdscpy(sds s, const char *t);

sds sdsMakeRoomFor(sds s, size_t addlen);
sds sdsRemoveFreeSpace(sds s);
#endif