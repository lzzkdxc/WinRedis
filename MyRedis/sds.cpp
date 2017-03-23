#include <stdio.h>
#include <string.h>
#include "sds.h"
#include "sdsalloc.h"

#pragma warning(disable:4267)

// 得到sds的header的大小
static inline int sdsHdrSize(char type)
{
	switch (type&SDS_TYPE_MASK)
	{
	case SDS_TYPE_5:
		return sizeof(sdshdr5);
	case SDS_TYPE_8:
		return sizeof(sdshdr8);
	case SDS_TYPE_16:
		return sizeof(sdshdr16);
	case SDS_TYPE_32:
		return sizeof(sdshdr32);
	case SDS_TYPE_64:
		return sizeof(sdshdr64);
	}
	return 0;
}

// 根据字符串长度判断header类型
static inline char sdsReqType(size_t string_size)
{
	if (string_size < 1 << 5)
		return SDS_TYPE_5;
	if (string_size < 1 << 8)
		return SDS_TYPE_8;
	if (string_size < 1 << 16)
		return SDS_TYPE_16;
	if (string_size < 1ll << 32)
		return SDS_TYPE_32;
	return SDS_TYPE_64;
}

// 申请一片连续的内存空间
sds sdsnewlen(const void *init, size_t initlen)
{
	void *sh;
	sds s;
	char type = sdsReqType(initlen);
	// 空的字符串通常被创建成type 8，因为type 5已经不实用了。
	if (type == SDS_TYPE_5 && initlen == 0) type = SDS_TYPE_8;
	int hdrlen = sdsHdrSize(type);
	unsigned char *fp; // flags字段的指针

	sh = s_malloc(hdrlen + initlen + 1);
	if (!init)
		memset(sh, 0, hdrlen + initlen + 1);
	if (sh == nullptr) return NULL;
	// s为数据部分的起始指针
	s = (char*)sh + hdrlen;
	fp = ((unsigned char *)s) - 1;
	switch (type)
	{
		case SDS_TYPE_5:
		{
			*fp = type | (initlen << SDS_TYPE_BITS);
			break;
		}
		case SDS_TYPE_8:
		{
			SDS_HDR_VAR(8, s);
			sh->len = initlen;
			sh->alloc = initlen;
			*fp = type;
			break;
		}
		case SDS_TYPE_16:
		{
			SDS_HDR_VAR(16, s);
			sh->len = initlen;
			sh->alloc = initlen;
			*fp = type;
			break;
		}
		case SDS_TYPE_32:
		{
			SDS_HDR_VAR(32, s);
			sh->len = initlen;
			sh->alloc = initlen;
			*fp = type;
			break;
		}
		case SDS_TYPE_64:
		{
			SDS_HDR_VAR(64, s);
			sh->len = initlen;
			sh->alloc = initlen;
			*fp = type;
			break;
		}
	}
	if (initlen && init)
		memcpy(s, init, initlen);
	s[initlen] = '\0';
	return s;
}