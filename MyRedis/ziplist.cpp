#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "zmalloc.h"
#include "endianconv.h"
#include "ziplist.h"
#include "redisassert.h"

#define ZIP_END 255
#define ZIP_BIGLEN 254

/* Different encoding/length possibilities */
#define ZIP_STR_MASK 0xc0       /* 1100 0000 */
#define ZIP_INT_MASK 0x30       /* 0011 0000 */
/* string */
#define ZIP_STR_06B (0 << 6)    /* 0000 0000 */
#define ZIP_STR_14B (1 << 6)    /* 0100 0000 */
#define ZIP_STR_32B (2 << 6)    /* 1000 0000 */
/* integer */
#define ZIP_INT_16B (0xc0 | 0<<4) /* 001100 */
#define ZIP_INT_32B (0xc0 | 1<<4) /* 011100 */
#define ZIP_INT_64B (0xc0 | 2<<4) /* 101100 */
#define ZIP_INT_24B (0xc0 | 3<<4) /* 111100 */
#define ZIP_INT_8B 0xfe
/* 4 bit integer immediate encoding */
#define ZIP_INT_IMM_MASK 0x0f   /* 0000 1111 */
#define ZIP_INT_IMM_MIN 0xf1    /* 1111 0001 */
#define ZIP_INT_IMM_MAX 0xfd    /* 1111 1101 */
#define ZIP_INT_IMM_VAL(v) (v & ZIP_INT_IMM_MASK)

/* �ж��Ƿ����ַ����� */
#define ZIP_IS_STR(enc) (((enc) & ZIP_STR_MASK) < ZIP_STR_MASK)

/* ���ߺ� */
/* ����ziplist��zlbytes�ֶ� */
#define ZIPLIST_BYTES(zl)		(*((uint32_t*)(zl)))
/* ����ziplist��zltail�ֶ� */
#define ZIPLIST_TAIL_OFFSET(zl)	(*((uint32_t*)((zl)+sizeof(uint32_t))))
/* ��ȡziplist��zllen�ֶ� */
#define ZIPLIST_LENGTH(zl)		(*((uint16_t*)((zl)+sizeof(uint32_t)*2)))
/* ziplistͷ�����ȣ� 4�ֽڵ�zlbytes + 4�ֽڵ�zltail + 2�ֽڵ�zllen */
#define ZIPLIST_HEADER_SIZE		(sizeof(uint32_t)*2+sizeof(uint16_t))
/* ziplistĩβ���� */
#define ZIPLIST_END_SIZE		(sizeof(uint8_t))
/* ��ȡziplist�ĵ�һ���ڵ���׵�ַ */
#define ZIPLIST_ENTRY_HEAD(zl)	((zl)+ZIPLIST_HEADER_SIZE)
/* ��ȡziplist�����һ���ڵ���׵�ַ */
#define ZIPLIST_ENTRY_TAIL(zl)	((zl)+intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)))
/* ��ȡziplist�Ľ�β�� */
#define ZIPLIST_ENTRY_END(zl)	((zl)+intrev32ifbe(ZIPLIST_BYTES(zl))-1)

// �ڵ�ṹ
typedef struct _zlentry 
{
	unsigned int prevrawlensize, prevrawlen; // ǰ�ýڵ㳤�Ⱥͱ������賤��
	unsigned int lensize, len;	// ��ǰ�ڵ㳤�Ⱥͱ������賤��
	unsigned int headersize;	// ͷ�Ĵ�С
	unsigned char encoding;		// ��������
	unsigned char *p;			// ���ݲ���
} zlentry;

// ��ptr����ȡ���룬�ѱ������ý�encoding��
#define ZIP_ENTRY_ENCODING(ptr, encoding) do { \
	(encoding) = (ptr[0]); \
	if ((encoding) < ZIP_STR_MASK) (encoding) &= ZIP_STR_MASK; \
} while (0);

// ���ش洢encoding����������ֽ���
unsigned int zipIntSize(unsigned char encoding)
{
	switch (encoding) {
	case ZIP_INT_8B:  return 1;
	case ZIP_INT_16B: return 2;
	case ZIP_INT_24B: return 3;
	case ZIP_INT_32B: return 4;
	case ZIP_INT_64B: return 8;
	default: return 0; /* 4 bit immediate */
	}
	assert(NULL);
	return 0;
}

/* ����rawlen��д��p�����pΪnull���򷵻ر���rawlen����������ֽ��� */
unsigned int zipEncodeLength(unsigned char *p, unsigned char encoding, unsigned int rawlen)
{
	unsigned int len = 1;
	unsigned char buf[5];

	if (ZIP_IS_STR(encoding))
	{
		/*��Ȼ�����˱��룬���������ܲ���Ϊ�ַ������õģ�
		��������������ʹ��ԭʼ������ȷ����*/
		if (rawlen <= 0x3f)
		{
			if (!p) return len;
			buf[0] = ZIP_STR_06B | rawlen;
		}
		else if (rawlen <= 0x3fff)
		{
			len += 1;
			if (!p) return len;
			buf[0] = ZIP_STR_14B | ((rawlen >> 8) & 3f);
			buf[1] = rawlen & 0xff;
		}
		else 
		{
			len += 4;
			if (!p) return len;
			buf[0] = ZIP_STR_32B;
			buf[1] = (rawlen >> 24) & 0xff;
			buf[2] = (rawlen >> 16) & 0xff;
			buf[3] = (rawlen >> 8) & 0xff;
			buf[4] = rawlen & 0xff;
		}
	}
	else
	{
		if (!p) return len;
		buf[0] = encoding;
	}

	memcpy(p, buf, len);
	return len;
}

/* ���볤��
encoding: �ڵ����
lensize: ����ڵ㳤��������ֽ���
len: �ڵ�����
*/
#define ZIP_DECODE_LENGTH(ptr, encoding, lensize, len) do { \
	ZIP_ENTRY_ENCODING((ptr), (encoding));					\
	if ((encoding) < ZIP_STR_MASK) {						\
		if ((encoding) == ZIP_STR_06B) {					\
			(lensize) = 1;									\
			(len) = (ptr)[0] & 0x3f;						\
		} else if ((encoding) == ZIP_STR_14B) {				\
			(lensize) = 2;									\
			(len) = (((ptr)[0] & 0x3f) << 8) | (ptr)[1];	\
		} else if ((encoding) == ZIP_STR_32B) {				\
			(lensize) = 5;									\
			(len) = ((ptr)[1] << 24) |						\
			((ptr)[2] << 16) |								\
			((ptr)[3] << 8) |								\
			((ptr)[4]);                                     \
		} else {											\
			assert(NULL);									\
		}													\
	} else {												\
		(lensize) = 1;										\
		(len) = zipIntSize(encoding);						\
	}														\
} while (0);

/**/
unsigned int zipPrevEncodeLength(unsigned char *p, unsigned int len)
{
	if (p == nullptr)
	{
		return (len < ZIP_BIGLEN) ? 1 : sizeof(len) + 1;
	}
	else
	{
		if (len < ZIP_BIGLEN)
		{
			p[0] = len;
			return 1;
		}
		else {
			p[0] = ZIP_BIGLEN;
			memcpy(p + 1, &len, sizeof(len));
			memrev32ifbe(p + 1);
			return 1 + sizeof(len);
		}
	}
}

unsigned int zipPrevEncodeLengthForceLarge(unsigned char *p, unsigned int len)
{
	if (p == nullptr) return;
	p[0] = ZIP_BIGLEN;
	memcpy(p + 1, &len, sizeof(len));
	memrev32ifbe(p+1);
}

#define ZIP_DECODE_PREVLENSIZE(ptr, prevlensize) do {	\
	if ((ptr)[0] < ZIP_BIGLEN) {						\
		(prevlensize) = 1;								\
	} else {											\
		(prevlensize) = 5;								\
	}													\
} while (0);

#define ZIP_DECODE_PREVLEN(ptr, prevlensize, prevlen) do {	\
	ZIP_DECODE_PREVLENSIZE(ptr, prevlensize);				\
	if ((prevlensize) == 1) {								\
		(prevlen) = (ptr)[0];								\
	} else if ((prevlensize) == 5) {						\
		assert(sizeof((prevlensize)) == 4);					\
		memcpy(&(prevlen), ((char*)(ptr))+1, 4);			\
		memrev32ifbe(&prevlen);								\
	}														\
} while (0);

// ����һ���յ�ziplist
unsigned char *ziplistNew()
{
	// ��ziplist�Ĵ�СΪ11���ֽڣ�ͷ��10�ֽڣ�β��1�ֽ�
	size_t bytes = ZIPLIST_HEADER_SIZE + 1;
	// �����ڴ�
	unsigned char *zl = (unsigned char *)zmalloc(bytes);
	// �趨ziplist������
	ZIPLIST_BYTES(zl) = intrev32ifbe(bytes); // �趨ziplist��ռ���ֽ��������б�Ҫ���д�С��ת��
	ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(ZIPLIST_HEADER_SIZE); // �趨β�ڵ����ͷ����ƫ����
	ZIPLIST_LENGTH(zl) = 0;	// �趨ziplist�ڵĽڵ���
	// �趨β��һ���ֽ�λ0xFF
	zl[bytes - 1] = ZIP_END;
	return zl;
}

// ����ڵ�ײ�ʵ��
unsigned char *__ziplistInsert(unsigned char *zl, unsigned char *p, unsigned char *s, unsigned int slen)
{
	size_t curlen = intrev32ifbe(ZIPLIST_BYTES(zl)); // ��ǰ����
	size_t reqlen; // ����ڵ����Ҫ�ĳ���
	unsigned int prevlensize = 0; // ǰ�ýڵ㳤��
	unsigned int prevlen = 0; // ����ó���ֵ����ĳ���
	size_t offset = 0;
	int  nextdiff = 0;
	unsigned char encoding = 0;
	long long value = 123456789; // Ϊ�˱��⾯�棬��ʼ����ֵ
	zlentry tail;

	// �ҳ�������ڵ��ǰ�ýڵ㳤��
	if (p[0] != ZIP_END) // ���p[0]��ָ���б�ĩ�ˣ�˵���б�ǿգ�����pָ������һ���ڵ�
	{
		// ����ǰ�ýڵ�p�ĳ��Ⱥͱ���ó�����Ҫ���ֽ�
		ZIP_DECODE_PREVLEN(p, prevlensize, prevlen);
	}
	else
	{
		// ���pָ���б�ĩ�ˣ���ʾ�б�Ϊ��
		unsigned char *ptail = ZIPLIST_ENTRY_TAIL(zl);

		if (ptail[0] != ZIP_END)
			prevlen = ziprawEntryLength(ptail); // ����β�ڵ�ĳ���
	}

	// �ж��Ƿ��ܹ�����Ϊ����
	if (zipTryEncoding(s, slen, &value, &encoding))
		reqlen = zipIntSize(encoding); // �ýڵ��Ѿ�����Ϊ������ͨ��encoding����ȡ���볤��
	else
		reqlen = slen; // �����ַ���������ýڵ�

	// ��ȡǰ�ýڵ�ı��볤��
	reqlen += zipPrevEncodeLength(nullptr, prevlen);
	// ��ȡ��ǰ�ڵ�ı��볤��
	reqlen += zipEncodeLength(nullptr, encoding, slen);

	// ֻҪ���ǲ��뵽�б��ĩ�ˣ�����Ҫ�жϵ�ǰp��ָ��Ľڵ�header�Ƿ��ܴ���½ڵ�ĳ��ȱ���
	// nextdiff�����¾ɱ���֮����ֽڴ�С�������ֵ����0
	// �Ǿ�˵����ǰpָ��Ľڵ��header������չ
	nextdiff = (p[0] != ZIP_END) ? zipPrevLenByteDiff(p, reqlen) : 0;

	// �洢p������б�zl��ƫ�Ƶ�ַ
	offset = p - zl;
	// ���·���ռ䣬curlen��ǰ�б�ĳ���
	// reqlen �½ڵ��ȫ������
	// nextdiff �½ڵ�ĺ�̽ڵ���չheader�ĳ���
	zl = ziplistResize(zl, curlen + reqlen + nextdiff);
	// ���»�ȡp��ֵ
	p = zl + offset;

	if (p[0] != ZIP_END)
	{
		// �ƶ�����Ԫ�أ�Ϊ��Ԫ�صĲ����ṩ�ռ�
		memmove(p + reqlen, p - nextdiff, curlen - offset - 1 + nextdiff);

		// p+reqlenΪ�½ڵ�ǰ�ýڵ��ƶ����λ�ã����½ڵ�ĳ��ȱ�����ǰ�ýڵ�
		zipPrevEncodeLength(p + reqlen, reqlen);

		// �����б�β����ڱ�ͷ��ƫ���������½ڵ�ĳ�������
		ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)) + reqlen);

		// ����½ڵ�����ж���ڵ㣬��ô��β��ƫ������Ҫ����nextdiff��ֵ
		zipEntry(p + reqlen, &tail);
		if (p[reqlen + tail.headersize + tail.len] != ZIP_END)
			ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl) + nextdiff));
	}
	else
	{
		ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(p - zl);
	}

	// ��nextdiff��Ϊ0ʱ����ʾ��Ҫ�½ڵ�ĺ�̽ڵ��ͷ��������չ
	if (nextdiff != 0)
	{
		offset = p - zl;
		// ��Ҫ��p��ָ��Ľڵ�header������չ����
		// �п��ܻ�������������
		zl = __ziplistCascadeUpdate(zl, p + reqlen);
		p = zl + offset;
	}

	// ���½ڵ�ǰ�ýڵ�ĳ���д���½ڵ��header
	p += zipPrevEncodeLength(p, prevlen);
	// ���½ڵ��ֵ����д���½ڵ��header
	p += zipEncodeLength(p, encoding, slen);
	// д��ڵ�ֵ
	if (ZIP_IS_STR(encoding))
		memcpy(p, s, slen);
	else
		zipSaveInteger(p, value, encoding);
	// �����б�ڵ����
	ZIPLIST_INCR_LENGTH(zl, 1);
	return zl;
}

/* ��ͷ��β������ڵ�
* zl: �������ziplist
* s��slen: ������ڵ���䳤��
* where: �������λ�ã�0����ͷ�����룬1����β������
*/
unsigned char *ziplistPush(unsigned char *zl, unsigned char *s, unsigned int slen, int where)
{
	unsigned char *p;
	// ��ȡ������λ�õ�ָ��
	p = (where == ZIPLIST_HEAD) ? ZIPLIST_ENTRY_HEAD(zl) : ZIPLIST_ENTRY_END(zl);
	return __ziplistInsert(zl, p, s, slen);
}
