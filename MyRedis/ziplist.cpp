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

/* 判断是否是字符串型 */
#define ZIP_IS_STR(enc) (((enc) & ZIP_STR_MASK) < ZIP_STR_MASK)

/* 工具宏 */
/* 访问ziplist的zlbytes字段 */
#define ZIPLIST_BYTES(zl)		(*((uint32_t*)(zl)))
/* 访问ziplist的zltail字段 */
#define ZIPLIST_TAIL_OFFSET(zl)	(*((uint32_t*)((zl)+sizeof(uint32_t))))
/* 获取ziplist的zllen字段 */
#define ZIPLIST_LENGTH(zl)		(*((uint16_t*)((zl)+sizeof(uint32_t)*2)))
/* ziplist头部长度： 4字节的zlbytes + 4字节的zltail + 2字节的zllen */
#define ZIPLIST_HEADER_SIZE		(sizeof(uint32_t)*2+sizeof(uint16_t))
/* ziplist末尾长度 */
#define ZIPLIST_END_SIZE		(sizeof(uint8_t))
/* 获取ziplist的第一个节点的首地址 */
#define ZIPLIST_ENTRY_HEAD(zl)	((zl)+ZIPLIST_HEADER_SIZE)
/* 获取ziplist的最后一个节点的首地址 */
#define ZIPLIST_ENTRY_TAIL(zl)	((zl)+intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)))
/* 获取ziplist的结尾符 */
#define ZIPLIST_ENTRY_END(zl)	((zl)+intrev32ifbe(ZIPLIST_BYTES(zl))-1)

// 节点结构
typedef struct _zlentry 
{
	unsigned int prevrawlensize, prevrawlen; // 前置节点长度和编码所需长度
	unsigned int lensize, len;	// 当前节点长度和编码所需长度
	unsigned int headersize;	// 头的大小
	unsigned char encoding;		// 编码类型
	unsigned char *p;			// 数据部分
} zlentry;

// 从ptr中提取编码，把编码设置进encoding中
#define ZIP_ENTRY_ENCODING(ptr, encoding) do { \
	(encoding) = (ptr[0]); \
	if ((encoding) < ZIP_STR_MASK) (encoding) &= ZIP_STR_MASK; \
} while (0);

// 返回存储encoding编码所需的字节数
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

/* 编码rawlen并写入p，如果p为null，则返回编码rawlen长度所需的字节数 */
unsigned int zipEncodeLength(unsigned char *p, unsigned char encoding, unsigned int rawlen)
{
	unsigned int len = 1;
	unsigned char buf[5];

	if (ZIP_IS_STR(encoding))
	{
		/*虽然给出了编码，但是它可能不是为字符串设置的，
		所以我们在这里使用原始长度来确定它*/
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

/* 解码长度
encoding: 节点编码
lensize: 保存节点长度所需的字节数
len: 节点数量
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

// 创建一个空的ziplist
unsigned char *ziplistNew()
{
	// 空ziplist的大小为11个字节，头部10字节，尾部1字节
	size_t bytes = ZIPLIST_HEADER_SIZE + 1;
	// 分配内存
	unsigned char *zl = (unsigned char *)zmalloc(bytes);
	// 设定ziplist的属性
	ZIPLIST_BYTES(zl) = intrev32ifbe(bytes); // 设定ziplist所占的字节数，如有必要进行大小端转换
	ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(ZIPLIST_HEADER_SIZE); // 设定尾节点相对头部的偏移量
	ZIPLIST_LENGTH(zl) = 0;	// 设定ziplist内的节点数
	// 设定尾部一个字节位0xFF
	zl[bytes - 1] = ZIP_END;
	return zl;
}

// 插入节点底层实现
unsigned char *__ziplistInsert(unsigned char *zl, unsigned char *p, unsigned char *s, unsigned int slen)
{
	size_t curlen = intrev32ifbe(ZIPLIST_BYTES(zl)); // 当前长度
	size_t reqlen; // 插入节点后需要的长度
	unsigned int prevlensize = 0; // 前置节点长度
	unsigned int prevlen = 0; // 编码该长度值所需的长度
	size_t offset = 0;
	int  nextdiff = 0;
	unsigned char encoding = 0;
	long long value = 123456789; // 为了避免警告，初始化其值
	zlentry tail;

	// 找出待插入节点的前置节点长度
	if (p[0] != ZIP_END) // 如果p[0]不指向列表末端，说明列表非空，并且p指向其中一个节点
	{
		// 解码前置节点p的长度和编码该长度需要的字节
		ZIP_DECODE_PREVLEN(p, prevlensize, prevlen);
	}
	else
	{
		// 如果p指向列表末端，表示列表为空
		unsigned char *ptail = ZIPLIST_ENTRY_TAIL(zl);

		if (ptail[0] != ZIP_END)
			prevlen = ziprawEntryLength(ptail); // 计算尾节点的长度
	}

	// 判断是否能够编码为整数
	if (zipTryEncoding(s, slen, &value, &encoding))
		reqlen = zipIntSize(encoding); // 该节点已经编码为整数，通过encoding来获取编码长度
	else
		reqlen = slen; // 采用字符串来编码该节点

	// 获取前置节点的编码长度
	reqlen += zipPrevEncodeLength(nullptr, prevlen);
	// 获取当前节点的编码长度
	reqlen += zipEncodeLength(nullptr, encoding, slen);

	// 只要不是插入到列表的末端，都需要判断当前p所指向的节点header是否能存放新节点的长度编码
	// nextdiff保存新旧编码之间的字节大小差，如果这个值大于0
	// 那就说明当前p指向的节点的header进行扩展
	nextdiff = (p[0] != ZIP_END) ? zipPrevLenByteDiff(p, reqlen) : 0;

	// 存储p相对于列表zl的偏移地址
	offset = p - zl;
	// 重新分配空间，curlen当前列表的长度
	// reqlen 新节点的全部长度
	// nextdiff 新节点的后继节点扩展header的长度
	zl = ziplistResize(zl, curlen + reqlen + nextdiff);
	// 重新获取p的值
	p = zl + offset;

	if (p[0] != ZIP_END)
	{
		// 移动现有元素，为新元素的插入提供空间
		memmove(p + reqlen, p - nextdiff, curlen - offset - 1 + nextdiff);

		// p+reqlen为新节点前置节点移动后的位置，将新节点的长度编码至前置节点
		zipPrevEncodeLength(p + reqlen, reqlen);

		// 更新列表尾相对于表头的偏移量，将新节点的长度算上
		ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)) + reqlen);

		// 如果新节点后面有多个节点，那么表尾的偏移量需要算上nextdiff的值
		zipEntry(p + reqlen, &tail);
		if (p[reqlen + tail.headersize + tail.len] != ZIP_END)
			ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl) + nextdiff));
	}
	else
	{
		ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(p - zl);
	}

	// 当nextdiff不为0时，表示需要新节点的后继节点对头部进行扩展
	if (nextdiff != 0)
	{
		offset = p - zl;
		// 需要对p所指向的节点header进行扩展更新
		// 有可能会引起连锁更新
		zl = __ziplistCascadeUpdate(zl, p + reqlen);
		p = zl + offset;
	}

	// 将新节点前置节点的长度写入新节点的header
	p += zipPrevEncodeLength(p, prevlen);
	// 将新节点的值长度写入新节点的header
	p += zipEncodeLength(p, encoding, slen);
	// 写入节点值
	if (ZIP_IS_STR(encoding))
		memcpy(p, s, slen);
	else
		zipSaveInteger(p, value, encoding);
	// 更新列表节点计数
	ZIPLIST_INCR_LENGTH(zl, 1);
	return zl;
}

/* 从头或尾部插入节点
* zl: 待插入的ziplist
* s，slen: 待插入节点和其长度
* where: 带插入的位置，0代表头部插入，1代表尾部插入
*/
unsigned char *ziplistPush(unsigned char *zl, unsigned char *s, unsigned int slen, int where)
{
	unsigned char *p;
	// 获取待插入位置的指针
	p = (where == ZIPLIST_HEAD) ? ZIPLIST_ENTRY_HEAD(zl) : ZIPLIST_ENTRY_END(zl);
	return __ziplistInsert(zl, p, s, slen);
}
