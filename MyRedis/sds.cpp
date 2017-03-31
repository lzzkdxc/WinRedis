#include <stdio.h>
#include <string.h>
#include "sds.h"
#include "sdsalloc.h"


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

// 创建一个空的字符串
sds sdsempty()
{
	return sdsnewlen("", 0);
}

// 复制字符串并返回一个新的字符串
sds sdsnew(const char *init)
{
	size_t initlen = (init == nullptr) ? 0 : strlen(init);
	return sdsnewlen(init, initlen);
}

// 复制字符串
sds sdsdup(const sds s)
{
	return sdsnewlen(s, sizeof(s));
}

// 释放内存
void sdsfree(sds s)
{
	if (s == nullptr) return;
	s_free((char*)s - sdsHdrSize(s[-1]));
}

// 在原有的字符串中取得更大的空间，并返回扩展空间后的字符串
sds sdsMakeRoomFor(sds s, size_t addlen)
{
	// 除去头后字符串起始的位置
	void *sh, *newsh;
	size_t avail = sdsavail(s);	// 剩余空间
	size_t len, newlen;
	char type;
	char oldtype = s[-1] & SDS_TYPE_MASK;
	int hdrlen;

	// 如果剩余的空间足够直接返回
	if (avail >= addlen) return s;

	len = strlen(s);
	sh = (char*)s - sdsHdrSize(oldtype);
	newlen = len + addlen;
	// 如果新的字符串长度没有超过1M则扩大1倍,超过则加上1M
	if (newlen < SDS_MAX_PREALLOC)
		newlen *= 2;
	else
		newlen += SDS_MAX_PREALLOC;

	type = sdsReqType(newlen);

	// type5已不能再使用，按SDS_TYPE_8处理
	if (type == SDS_TYPE_5) type = SDS_TYPE_8;
	
	// 获取新类型的头长度
	hdrlen = sdsHdrSize(type);
	if (oldtype == type)
	{
		// 如果与原类型相同，直接调用realloc函数扩充内存
		// 因为header没有变，所以不需要重新设置长度
		newsh = s_realloc(sh, hdrlen + newlen + 1);
		if (newsh == nullptr) return NULL;
		s = (char*)newsh + hdrlen;
	}
	else
	{
		// 如果类型调整了，header的大小就需要调整
		// 这时就需要移动buf[]部分，所以不能使用realloc
		newsh = s_malloc(hdrlen + newlen + 1);
		if (newsh == nullptr) return NULL;
		memcpy((char*)newsh + hdrlen, s, len + 1);
		s_free(sh);
		s = (char*)newsh + hdrlen;
		s[-1] = type;
		sdssetlen(s, len); 
	}
	sdssetalloc(s, newlen);
	return s;
}

// 用来回收sds空余空间，压缩内存，函数调用后，s会无效
// 实际上，就是重新分配一块内存，将原有数据拷贝到新内存上，并释放原有空间
// 新内存的大小比原来小了alloc-len大小
sds sdsRemoveFreeSpace(sds s)
{
	void *sh, *newsh;
	char type;
	char oldtype = s[-1] & SDS_TYPE_MASK;
	int hdrlen;
	size_t len = sdslen(s);
	sh = (char*)s - sdsHdrSize(oldtype);

	type = sdsReqType(len);
	hdrlen = sdsHdrSize(type);
	if (oldtype == type)
	{
		newsh = s_realloc(sh, len + hdrlen + 1);
		if (newsh == nullptr) return NULL;
		s = (char*)newsh + hdrlen;
	}
	else
	{
		newsh = s_malloc(len + hdrlen + 1);
		if (newsh == nullptr) return NULL;
		memcpy((char*)newsh + hdrlen, s, len + 1);
		s_free(sh);
		s = (char*)newsh + hdrlen;
		s[-1] = type;
		sdssetlen(s, len);
	}
	sdssetalloc(s, len);
	return s;
}

// 扩展字符串到指定长度
sds sdsgrowzero(sds s, size_t len)
{
	size_t curlen = sdslen(s);
	if (len < curlen) return s;

	s = sdsMakeRoomFor(s, len - curlen);
	if (s == NULL) return NULL;

	// 初始化新扩展的字符串
	memset(s + curlen, 0, (len - curlen + 1));
	sdssetlen(s, len);
	return s;
}

// 连接两个字符串
sds sdscatlen(sds s, const void *t, size_t len)
{
	size_t curlen = sdslen(s);

	s = sdsMakeRoomFor(s, len);
	if (s == NULL) return NULL;

	// 连接新字符串
	memcpy(s + curlen, t, len);
	sdssetlen(s, curlen + len);
	s[curlen + len] = '\0';
	return s;
}

// 把一个以空字符为结尾的字符串添加到sds末尾
sds sdscat(sds s, const char *t)
{
	return sdscatlen(s, t, strlen(t));
}

// 连接两个sds
sds sdscatsds(sds s, const sds t)
{
	return sdscatlen(s, t, sdslen(t));
}

// 字符串的复制
sds sdscpylen(sds s, const char *t, size_t len)
{
	if (sdsalloc(s) < len)
	{
		s = sdsMakeRoomFor(s, len - sdsalloc(s));
		if (s == NULL) return NULL;
	}
	memcpy(s, t, len);
	s[len] = '\0';
	sdssetlen(s, len);
	return s;
}

// 字符串的复制
sds sdscpy(sds s, const char *t)
{
	return sdscpylen(s, t, strlen(t));
}

// 把长整型转化成字符串,并返回字符串长度
#define SDS_LLSTR_SIZE 21
size_t sdsll2str(char *s, long long value)
{
	char *p;
	char tmp;
	unsigned long long v;
	size_t l;

	v = (value < 0) ? -value : value;
	p = s;
	do 
	{
		// 通过求余来拼接字符串
		*p++ = '0' + (v % 10);
		v /= 10;
	} while (v);
	if (value < 0) *p++ = '-';

	// 计算长度，并添加空字符串结束符
	l = p - s;
	*p = '\0';

	// 反转字符串
	p--;
	while (s < p)
	{
		tmp = *s;
		*s = *p;
		*p = tmp;
		s++;
		p--;
	}
	return l;
}

// 通过一个长整型的数字创建字符串
sds sdsfromlonglong(long long value)
{
	char buf[SDS_LLSTR_SIZE];
	int len = sdsll2str(buf, value);

	return sdsnewlen(buf, len);
}