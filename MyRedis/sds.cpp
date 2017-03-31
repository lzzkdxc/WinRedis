#include <stdio.h>
#include <string.h>
#include "sds.h"
#include "sdsalloc.h"


// �õ�sds��header�Ĵ�С
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

// �����ַ��������ж�header����
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

// ����һƬ�������ڴ�ռ�
sds sdsnewlen(const void *init, size_t initlen)
{
	void *sh;
	sds s;
	char type = sdsReqType(initlen);
	// �յ��ַ���ͨ����������type 8����Ϊtype 5�Ѿ���ʵ���ˡ�
	if (type == SDS_TYPE_5 && initlen == 0) type = SDS_TYPE_8;
	int hdrlen = sdsHdrSize(type);
	unsigned char *fp; // flags�ֶε�ָ��

	sh = s_malloc(hdrlen + initlen + 1);
	if (!init)
		memset(sh, 0, hdrlen + initlen + 1);
	if (sh == nullptr) return NULL;
	// sΪ���ݲ��ֵ���ʼָ��
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

// ����һ���յ��ַ���
sds sdsempty()
{
	return sdsnewlen("", 0);
}

// �����ַ���������һ���µ��ַ���
sds sdsnew(const char *init)
{
	size_t initlen = (init == nullptr) ? 0 : strlen(init);
	return sdsnewlen(init, initlen);
}

// �����ַ���
sds sdsdup(const sds s)
{
	return sdsnewlen(s, sizeof(s));
}

// �ͷ��ڴ�
void sdsfree(sds s)
{
	if (s == nullptr) return;
	s_free((char*)s - sdsHdrSize(s[-1]));
}

// ��ԭ�е��ַ�����ȡ�ø���Ŀռ䣬��������չ�ռ����ַ���
sds sdsMakeRoomFor(sds s, size_t addlen)
{
	// ��ȥͷ���ַ�����ʼ��λ��
	void *sh, *newsh;
	size_t avail = sdsavail(s);	// ʣ��ռ�
	size_t len, newlen;
	char type;
	char oldtype = s[-1] & SDS_TYPE_MASK;
	int hdrlen;

	// ���ʣ��Ŀռ��㹻ֱ�ӷ���
	if (avail >= addlen) return s;

	len = strlen(s);
	sh = (char*)s - sdsHdrSize(oldtype);
	newlen = len + addlen;
	// ����µ��ַ�������û�г���1M������1��,���������1M
	if (newlen < SDS_MAX_PREALLOC)
		newlen *= 2;
	else
		newlen += SDS_MAX_PREALLOC;

	type = sdsReqType(newlen);

	// type5�Ѳ�����ʹ�ã���SDS_TYPE_8����
	if (type == SDS_TYPE_5) type = SDS_TYPE_8;
	
	// ��ȡ�����͵�ͷ����
	hdrlen = sdsHdrSize(type);
	if (oldtype == type)
	{
		// �����ԭ������ͬ��ֱ�ӵ���realloc���������ڴ�
		// ��Ϊheaderû�б䣬���Բ���Ҫ�������ó���
		newsh = s_realloc(sh, hdrlen + newlen + 1);
		if (newsh == nullptr) return NULL;
		s = (char*)newsh + hdrlen;
	}
	else
	{
		// ������͵����ˣ�header�Ĵ�С����Ҫ����
		// ��ʱ����Ҫ�ƶ�buf[]���֣����Բ���ʹ��realloc
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

// ��������sds����ռ䣬ѹ���ڴ棬�������ú�s����Ч
// ʵ���ϣ��������·���һ���ڴ棬��ԭ�����ݿ��������ڴ��ϣ����ͷ�ԭ�пռ�
// ���ڴ�Ĵ�С��ԭ��С��alloc-len��С
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

// ��չ�ַ�����ָ������
sds sdsgrowzero(sds s, size_t len)
{
	size_t curlen = sdslen(s);
	if (len < curlen) return s;

	s = sdsMakeRoomFor(s, len - curlen);
	if (s == NULL) return NULL;

	// ��ʼ������չ���ַ���
	memset(s + curlen, 0, (len - curlen + 1));
	sdssetlen(s, len);
	return s;
}

// ���������ַ���
sds sdscatlen(sds s, const void *t, size_t len)
{
	size_t curlen = sdslen(s);

	s = sdsMakeRoomFor(s, len);
	if (s == NULL) return NULL;

	// �������ַ���
	memcpy(s + curlen, t, len);
	sdssetlen(s, curlen + len);
	s[curlen + len] = '\0';
	return s;
}

// ��һ���Կ��ַ�Ϊ��β���ַ�����ӵ�sdsĩβ
sds sdscat(sds s, const char *t)
{
	return sdscatlen(s, t, strlen(t));
}

// ��������sds
sds sdscatsds(sds s, const sds t)
{
	return sdscatlen(s, t, sdslen(t));
}

// �ַ����ĸ���
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

// �ַ����ĸ���
sds sdscpy(sds s, const char *t)
{
	return sdscpylen(s, t, strlen(t));
}

// �ѳ�����ת�����ַ���,�������ַ�������
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
		// ͨ��������ƴ���ַ���
		*p++ = '0' + (v % 10);
		v /= 10;
	} while (v);
	if (value < 0) *p++ = '-';

	// ���㳤�ȣ�����ӿ��ַ���������
	l = p - s;
	*p = '\0';

	// ��ת�ַ���
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

// ͨ��һ�������͵����ִ����ַ���
sds sdsfromlonglong(long long value)
{
	char buf[SDS_LLSTR_SIZE];
	int len = sdsll2str(buf, value);

	return sdsnewlen(buf, len);
}