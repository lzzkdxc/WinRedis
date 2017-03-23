#include <stdio.h>
#include <string.h>
#include "sds.h"
#include "sdsalloc.h"

#pragma warning(disable:4267)

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