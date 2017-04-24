#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "zmalloc.h"
#include "endianconv.h"
#include "util.h"
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

#define INT24_MAX 0x7fffff
#define INT24_MIN (-INT24_MAX - 1)

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

/* �����ڵ������ */
#define ZIPLIST_INCR_LENGTH(zl, incr) { \
	if (ZIPLIST_LENGTH(zl) < UINT16_MAX) \
		ZIPLIST_LENGTH(zl) = intrev16ifbe(intrev16ifbe(ZIPLIST_LENGTH(zl)) + incr); \
}

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
			buf[0] = ZIP_STR_14B | ((rawlen >> 8) & 0x3f);
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

/* ������һ���ڵ�ĳ��ȣ���д�뵽p.
���p��null, �򷵻ر�������ڵ㳤��������ֽ���*/
unsigned int zipPrevEncodeLength(unsigned char *p, unsigned int len)
{
	if (p == NULL)
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

/* ������һ���ڵ�ĳ��ȣ���д�뵽p
 ���ֻ�ڴ�˵������ʹ�ã���__ziplistCascadeUpdate�б����� */
void zipPrevEncodeLengthForceLarge(unsigned char *p, unsigned int len)
{
	if (p == NULL) return;
	p[0] = ZIP_BIGLEN;
	memcpy(p + 1, &len, sizeof(len));
	memrev32ifbe(p+1);
}

/* ������һ��Ԫ�صĳ���������ֽ��� */
#define ZIP_DECODE_PREVLENSIZE(ptr, prevlensize) do {	\
	if ((ptr)[0] < ZIP_BIGLEN) {						\
		(prevlensize) = 1;								\
	} else {											\
		(prevlensize) = 5;								\
	}													\
} while (0);

/* ����ǰһ��Ԫ�صĳ��� */
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

/* ����pָ���ǰһ���ڵ�ĳ�����洢len���������ֽ����ϵĲ��� */
int zipPrevLenByteDiff(unsigned char *p, unsigned int len)
{
	unsigned int prevlensize;
	ZIP_DECODE_PREVLENSIZE(p, prevlensize);
	return zipPrevEncodeLength(NULL, len) - prevlensize;
}

/* ����pָ��Ľڵ�ʹ�õ����ֽ��� */
unsigned int zipRawEntryLength(unsigned char *p)
{
	unsigned int prevlensize, encoding, lensize, len;
	ZIP_DECODE_PREVLENSIZE(p, prevlensize);
	ZIP_DECODE_LENGTH(p + prevlensize, encoding, lensize, len);
	return prevlensize + lensize + len;
}

/* ���'entry'ָ����ַ����Ƿ���Ա�����Ϊ����
�洢������v */
int zipTryEncoding(unsigned char *entry, unsigned int entrylen, long long *v, unsigned char *encoding)
{
	long long value;

	if (entrylen >= 32 || entrylen == 0) return 0;
	if (string2ll((char*)entry, entrylen, &value) == 1)
	{
		if (value >= 0 && value <= 12)
			*encoding = ZIP_INT_IMM_MIN + value;
		else if (value >= INT8_MIN && value <= INT8_MAX)
			*encoding = ZIP_INT_8B;
		else if (value >= INT16_MIN && value <= INT16_MAX)
			*encoding = ZIP_INT_16B;
		else if (value >= INT24_MIN && value <= INT24_MAX)
			*encoding = ZIP_INT_24B;
		else if (value >= INT32_MIN && value <= INT32_MAX)
			*encoding = ZIP_INT_32B;
		else 
			*encoding = ZIP_INT_64B;
		*v = value;
		return 1;
	}
	return 0;
}

/* ����encoding���ͣ���valueд��p */
void zipSaveInteger(unsigned char *p, int64_t value, unsigned char encoding)
{
	int16_t i16;
	int32_t i32;
	int64_t i64;
	if (encoding == ZIP_INT_8B)
	{
		((int8_t*)p)[0] = (int8_t)value;
	}
	else if (encoding == ZIP_INT_16B)
	{
		i16 = value;
		memcpy(p, &i16, sizeof(i16));
		memrev16ifbe(p);
	}
	else if (encoding == ZIP_INT_24B)
	{
		i32 = value << 8;
		memrev32ifbe(&i32);
		memcpy(p, ((uint8_t*)&i32) + 1, sizeof(i32) - sizeof(uint8_t));
	}
	else if (encoding == ZIP_INT_32B)
	{
		i32 = value;
		memcpy(p, &i32, sizeof(i32));
		memrev32ifbe(p);
	}
	else if (encoding == ZIP_INT_64B)
	{
		i64 = value;
		memcpy(p, &i64, sizeof(i64));
		memrev32ifbe(p);
	}
	else if (encoding >= ZIP_INT_IMM_MIN && encoding <= ZIP_INT_IMM_MAX)
	{
		/*�����κδ���*/
	}
	else
	{
		assert(NULL);
	}
}

/* ��ȡ��'p'����Ϊ'encoding'������ */
int64_t zipLoadInteger(unsigned char *p, unsigned char encoding)
{
	int16_t i16;
	int32_t i32;
	int64_t i64, ret = 0;
	if (encoding == ZIP_INT_8B)
	{
		ret = ((int8_t*)p)[0];
	}
	else if (encoding == ZIP_INT_16B)
	{
		memcpy(&i16, p, sizeof(i16));
		memrev16ifbe(&i16);
		ret = i16;
	}
	else if (encoding == ZIP_INT_24B)
	{
		i32 = 0;
		memcpy(((uint8_t*)&i32) + 1, p, sizeof(i32) - sizeof(uint8_t));
		memrev32ifbe(&i32);
		ret = i32 >> 8;
	}
	else if (encoding == ZIP_INT_32B)
	{
		memcpy(&i32, p, sizeof(i32));
		memrev32ifbe(&i32);
		ret = i32;
	}
	else if (encoding == ZIP_INT_64B)
	{
		memcpy(&i64, p, sizeof(i64));
		memrev64ifbe(&i64);
		ret = i64;
	}
	else if (encoding >= ZIP_INT_IMM_MIN && encoding <= ZIP_INT_IMM_MAX)
	{
		ret = (encoding & ZIP_INT_IMM_MASK) - 1;
	}
	else
	{
		assert(NULL);
	}
	return ret;
}

void zipEntry(unsigned char *p, zlentry *e)
{
	ZIP_DECODE_PREVLEN(p, e->prevrawlensize, e->prevrawlen);
	ZIP_DECODE_LENGTH(p + e->prevrawlensize, e->encoding, e->lensize, e->len);
	e->headersize = e->prevrawlensize + e->lensize;
	e->p = p;
}

/* ����һ���յ�ziplist */
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

/* ����ziplist��С */
unsigned char *ziplistResize(unsigned char *zl, unsigned int len)
{
	zl = (unsigned char *)zrealloc(zl, len);
	ZIPLIST_BYTES(zl) = intrev32ifbe(len);
	zl[len - 1] = ZIP_END;
	return zl;
}
/* -----------------------------------ziplist �ײ�ʵ��-----------------------------------------*/
/**
* ����һ���½ڵ���ӵ�ĳ���ڵ�֮ǰ��ʱ�����ԭ�ڵ��prevlen�����Ա����½ڵ�ĳ��ȣ�
* ��ô����Ҫ��ԭ�ڵ�Ŀռ������չ���� 1 �ֽ���չ�� 5 �ֽڣ���
*
* ���ǣ�����ԭ�ڵ������չ֮��ԭ�ڵ����һ���ڵ�� prevlen ���ܳ��ֿռ䲻�㣬
* ��������ڶ�������ڵ�ĳ��ȶ��ӽ� ZIP_BIGLEN ʱ���ܷ�����
*
* ������������ڴ�������������չ������
*
* ��Ϊ�ڵ�ĳ��ȱ�С�������������СҲ�ǿ��ܳ��ֵģ�������Ϊ�˱�����չ-��С-��չ-��С����������������֣�flapping����������
* ���ǲ���������������������� prevlen ������ĳ��ȸ���
*
* ���Ӷȣ�O(N^2)
*
* ����ֵ�����º�� ziplist
* zl: ziplist�׵�ַ��p:��Ҫ��չprevlensize�Ľڵ��׵�ַ
*/
unsigned char *__ziplistCascadeUpdate(unsigned char *zl, unsigned char *p)
{
	size_t curlen = intrev32ifbe(ZIPLIST_BYTES(zl));
	size_t rawlen, rawlensize;
	size_t offset, noffset, extra;
	unsigned char *np;
	zlentry cur, next;

	while (p[0] != ZIP_END)
	{
		// ��p��ָ��ڵ����Ϣ���浽cur�ṹ����
		zipEntry(p, &cur);
		// ��ǰ�ڵ�ĳ���
		rawlen = cur.headersize + cur.len;
		// ���뵱ǰ�ڵ�ĳ���������ֽ���
		rawlensize = zipPrevEncodeLength(NULL, rawlen);

		// ����û�������ڵ���
		if (p[rawlen] == ZIP_END) break;
		zipEntry(p + rawlen, &next);

		// ǰ��һ���ڵ�ĳ���û�иı� 
		if (next.prevrawlen == rawlen) break;

		// ��һ�ڵ�ĳ��ȱ���ռ䲻�㣬������չ  
		if (next.prevrawlensize < rawlensize)
		{
			offset = p - zl;
			// ��Ҫ��չ���ֽ���
			extra = rawlensize - next.prevrawlensize;
			zl = ziplistResize(zl, curlen + extra);
			p = zl + offset;

			// ��ǰָ�����һ��Ԫ�ص�ƫ����
			np = p + rawlen;
			noffset = np - zl;

			// ����һ��Ԫ�ز������һ��Ԫ��ʱ������ƫ����
			if ((zl + intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))) != np)
			{
				ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)) + extra);
			}

			// ���ɵ���һ���ڵ�next����������ziplistβ��ȫ�����ƫ�ƣ������rawlensize���ֽ������洢�ϸ��ڵ�ĳ��� 
			memmove(np + rawlensize, np + next.prevrawlensize, curlen - noffset - next.prevrawlensize - 1);
			zipPrevEncodeLength(np, rawlen);

			// ��һ���ڵ�
			p += rawlen;
			// ���µ�ǰziplist�ĳ���
			curlen += extra;
		}
		else
		{
			// ��һ�ڵ�ĳ��ȱ���ռ��ж��࣬������������ֻ�ǽ�������ĳ���д��ռ�
			if (next.prevrawlensize > rawlensize)
				zipPrevEncodeLengthForceLarge(p + rawlen, rawlen);
			else
				zipPrevEncodeLength(p + rawlen, rawlen);

			// �˳�������ռ��㹻�������ռ䲻��Ҫ����
			break;
		}
	}
	return zl;
}

/* ��p��ʼɾ��num���ڵ� */
unsigned char *__ziplistDelete(unsigned char *zl, unsigned char *p, unsigned int num)
{
	unsigned int i, totlen;
	int deleted = 0;
	size_t offset;
	int nextdiff = 0;
	zlentry first, tail;

	// ɾ�����׸��ڵ�
	zipEntry(p, &first);
	// ����num���ڵ�ռ�õ��ڴ�
	for (i = 0; p[0] != ZIP_END && i < num; i++)
	{
		// ƫ�Ƶ���һ���ڵ�
		p += zipRawEntryLength(p);
		deleted++;
	}

	// ��ɾ���Ľڵ�����ֽ���
	totlen = p - first.p;
	if (totlen > 0)
	{
		if (p[0] != ZIP_END)
		{
			// ����ɾ���ĵ�һ���ڵ�first��prevrawlensize��p�ڵ�prevrawlensize�Ĳ�ֵ  
			nextdiff = zipPrevLenByteDiff(p, first.prevrawlen);
			//����nextdiffֵ����p������ǰ�����ƫ�ƣ���ȡ���ֽ�������first.prevrawlen
			p -= nextdiff;
			// ��first.prevrawlenֵ�洢��p��prevrawlensize��
			zipPrevEncodeLength(p, first.prevrawlen);

			ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)) - totlen);

			// ���p����β�ڵ㣬��ôβ�ڵ�ָ����׵�ַ����Ҫ����nextdiff
			zipEntry(p, &tail);
			if (p[tail.headersize + tail.len] != ZIP_END)
			{
				ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)) + nextdiff);
			}

			// first.p��p֮��Ľڵ㶼����Ҫɾ���ģ������Ҫ��p��ʼ��������ǰƫ�ƣ�zlend����Ҫ���������Ҫ-1 
			memmove(first.p, p, intrev32ifbe(ZIPLIST_BYTES(zl)) - (p - zl) - 1);
		}
		else
		{
			// ����Ѿ�ɾ����zlend����ôβ�ڵ�ָ��Ӧ��ָ��ɾ����first֮ǰ�Ľڵ��׵�ַ
			ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe((first.p - zl) - first.prevrawlen);
		}

		//  ��С�ڴ沢����ziplist�ĳ���
		offset = first.p - zl;
		zl = ziplistResize(zl, intrev32ifbe(ZIPLIST_BYTES(zl)) - totlen + nextdiff);
		ZIPLIST_INCR_LENGTH(zl, -deleted);
		p = zl + offset;

		// ���nextdiff������0��˵�����ڵ�p�ڵ�ĳ��ȱ��ˣ���Ҫ���������¸��ڵ��ܷ񱣴�p�ڵ�ĳ���ֵ
		if (nextdiff != 0)
			zl = __ziplistCascadeUpdate(zl, p);
	}
	return zl;
}

/* ����ڵ�ײ�ʵ�� */
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
			prevlen = zipRawEntryLength(ptail); // ����β�ڵ�ĳ���
	}

	// �ж��Ƿ��ܹ�����Ϊ����
	if (zipTryEncoding(s, slen, &value, &encoding))
		reqlen = zipIntSize(encoding); // �ýڵ��Ѿ�����Ϊ������ͨ��encoding����ȡ���볤��
	else
		reqlen = slen; // �����ַ���������ýڵ�

	// ��ȡǰ�ýڵ�ı��볤��
	reqlen += zipPrevEncodeLength(NULL, prevlen);
	// ��ȡ��ǰ�ڵ�ı��볤��
	reqlen += zipEncodeLength(NULL, encoding, slen);

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

/*---------------------------------ziplist �����ӿ�---------------------------------------------*/

/* �ϲ�����ѹ������,�ѵڶ����ӵ���һ���ĺ��棬����һ���µ�ziplist */
unsigned char *ziplistMerge(unsigned char **first, unsigned char **second)
{
	// �����һ��������NULL, ���ܽ��кϲ�
	if (first == NULL || *first == NULL || second == NULL || *second == NULL)
		return NULL;

	// ���ܺϲ�������ͬ������
	if (*first == *second)
		return NULL;

	size_t first_bytes = intrev32ifbe(ZIPLIST_BYTES(*first));
	size_t first_len = intrev16ifbe(ZIPLIST_LENGTH(*first));

	size_t second_bytes = intrev32ifbe(ZIPLIST_BYTES(*second));
	size_t second_len = intrev16ifbe(ZIPLIST_LENGTH(*second));

	int append;
	unsigned char *source, *target;
	size_t target_bytes, source_bytes;

	// ѡ������ziplist���Ա����ǿ������ɵص�����С
	if (first_len >= second_len)
	{
		// ����first, ��second���ӵ�first��
		target = *first;
		target_bytes = first_bytes;
		source = *second;
		second_bytes = second_bytes;
		append = 1;
	}
	else
	{
		// ����second, ��firstǰ�õ�second
		target = *second;
		target_bytes = second_bytes;
		source = *first;
		source_bytes = first_bytes;
		append = 0;
	}

	// �������յ��ֽ���(��ȥһ��Ԫ����)
	size_t zlbytes = first_bytes + second_bytes - ZIPLIST_HEADER_SIZE - ZIPLIST_END_SIZE;
	size_t zllenth = first_len + second_len;
	zllenth = zllenth < UINT16_MAX ? zllenth : UINT16_MAX;

	// ����ƫ����
	size_t first_offset = intrev32ifbe(ZIPLIST_TAIL_OFFSET(*first));
	size_t second_offset = intrev32ifbe(ZIPLIST_TAIL_OFFSET(*second));

	// ���·����ڴ�
	target = (unsigned char *)zrealloc(target, zlbytes);
	if (append == 1)
	{
		memcpy(target + target_bytes - ZIPLIST_END_SIZE,
			source + ZIPLIST_HEADER_SIZE,
			source_bytes - ZIPLIST_HEADER_SIZE);
	}
	else
	{
		memmove(target + source_bytes - ZIPLIST_END_SIZE,
			target + ZIPLIST_HEADER_SIZE,
			target_bytes - ZIPLIST_HEADER_SIZE);
		memcpy(target, source, source_bytes - ZIPLIST_END_SIZE);
	}

	// ����ͷԪ����
	ZIPLIST_BYTES(target) = intrev32ifbe(zlbytes);
	ZIPLIST_LENGTH(target) = intrev16ifbe(zllenth);
	ZIPLIST_TAIL_OFFSET(target) = intrev32ifbe(
									(first_bytes - ZIPLIST_END_SIZE) +
									(second_offset - ZIPLIST_HEADER_SIZE));

	target = __ziplistCascadeUpdate(target, target + first_offset);

	// �ͷ��ڴ�
	if (append == 1)
	{
		zfree(*second);
		*second = NULL;
		*first = target;
	}
	else
	{
		zfree(*first);
		*first = NULL;
		*second = target;
	}
	return target;
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

/* ����index��ֵ����ȡѹ���б��index���ڵ� */
unsigned char *ziplistIndex(unsigned char *zl, int index)
{
	unsigned char *p;
	unsigned int prelensize, prelen = 0;
	// indexΪ������β����ʼ����
	if (index < 0)
	{
		index = (-index) - 1;
		// ��ȡβָ��
		p = ZIPLIST_ENTRY_TAIL(zl);
		if (p[0] != ZIP_END)
		{
			ZIP_DECODE_PREVLEN(p, prelensize, prelen);
			while (prelen > 0 && index--)
			{
				p -= prelen;
				ZIP_DECODE_PREVLEN(p, prelensize, prelen);
			}
		}
	}
	else
	{
		p = ZIPLIST_ENTRY_HEAD(zl);
		while (p[0] != ZIP_END && index--)
		{
			// ��ȡ��ǰ�ڵ�����峤�ȣ�����pre_entry_length��encoding��contents������
			p += zipRawEntryLength(p);
		}
	}
	return (p[0] == ZIP_END || index > 0) ? NULL : p;
}

/* ���p����һ���ڵ㣬���p��β�ڵ㣬����NULL */
unsigned char *ziplistNext(unsigned char *zl, unsigned char *p)
{
	((void)zl); //����������ɶ����

	if (p[0] == ZIP_END)
	{
		return NULL;
	}

	p += zipRawEntryLength(p);
	if (p[0] == ZIP_END)
	{
		return NULL;
	}

	return p;
}

/* ���p��ǰһ���ڵ� */
unsigned char *ziplistPrev(unsigned char *zl, unsigned char *p)
{
	unsigned int prevlensize, prevlen = 0;

	if (p[0] == ZIP_END)
	{
		p = ZIPLIST_ENTRY_TAIL(zl);
		return (p[0] == ZIP_END) ? NULL : p;
	}
	else if (p == ZIPLIST_ENTRY_HEAD(zl))
	{
		// ���p��ͷ�ڵ㣬��û��ǰ�ýڵ�
		return NULL;
	}
	else
	{
		ZIP_DECODE_PREVLEN(p, prevlensize, prevlen);
		assert(prevlen > 0);
		return p - prevlen;
	}
}

/* ���ָ��pָ��Ľڵ㣬����encoding����ֱ�洢��sstr����lval�� */
unsigned int ziplistGet(unsigned char *p, unsigned char **sval, unsigned int *slen, long long *lval)
{
	zlentry entry;
	if (p == NULL || p[0] == ZIP_END) return 0;
	if (sval) *sval = NULL;

	zipEntry(p, &entry);
	if (ZIP_IS_STR(entry.encoding))
	{
		if (sval)
		{
			*slen = entry.len;
			*sval = p + entry.headersize;
		}
	}
	else
	{
		if (lval)
			*lval = zipLoadInteger(p + entry.headersize, entry.encoding);
	}
	return 1;
}

/* ����ڵ�*/
unsigned char *ziplistInsert(unsigned char *zl, unsigned char *p, unsigned char *s, unsigned int slen)
{
	return __ziplistInsert(zl, p, s, slen);
}

/* ��ָ��p�����һ���ڵ�ɾ�� */
unsigned char *ziplistDelete(unsigned char *zl, unsigned char **p)
{
	size_t offset = *p - zl;
	zl = __ziplistDelete(zl, *p, 1);

	*p = zl + offset;
	return zl;
}

/* ��index�±꿪ʼɾ��num���ڵ� */
unsigned char *ziplistDeleteRange(unsigned char *zl, int index, unsigned int num)
{
	unsigned char *p = ziplistIndex(zl, index);
	return (p == NULL) ? zl : __ziplistDelete(zl, p, num);
}

unsigned int ziplistCompare(unsigned char *p, unsigned char *s, unsigned int slen)
{
	return 0;
}

unsigned char *ziplistFind(unsigned char *p, unsigned char *vstr, unsigned int vlen, unsigned int skip)
{
	return NULL;
}

/* ����ziplist�ĳ��� */
unsigned int ziplistLen(unsigned char *zl)
{
	unsigned int len = 0;
	if (intrev16ifbe(ZIPLIST_LENGTH(zl)) < UINT16_MAX)
	{
		len = intrev16ifbe(ZIPLIST_LENGTH(zl));
	}
	else
	{
		unsigned char *p = zl + ZIPLIST_HEADER_SIZE;
		while (*p != ZIP_END)
		{
			p += zipRawEntryLength(p);
			len++;
		}

		if (len < UINT16_MAX)
		{
			ZIPLIST_LENGTH(zl) = intrev16ifbe(len);
		}
	}

	return len;
}

/* ����ziplist blob��С�����ֽ�Ϊ��λ�� */
unsigned int ziplistBlobLen(unsigned char *zl)
{
	return intrev32ifbe(ZIPLIST_BYTES(zl));
}