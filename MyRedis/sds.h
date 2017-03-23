#ifndef __SDS_H
#define __SDS_H

#include "stdint.h"

/*
——————————————————————————————————
|-----header----|------data------|
----------------------------------
|len|alloc|flags|r|e|d|i|s|\0|...|
——————————————————————————————————
*/

typedef char* sds;

#pragma pack(push, 1)
/* Note: sdshdr5 is never used, we just access the flags byte directly.
* However is here to document the layout of type 5 SDS strings. */
typedef struct
{
	unsigned char flags;
	char buf[1];
}sdshdr5;

typedef struct  
{
	uint8_t len;			//表示字符串真正的长度，不包含空终止字符
	uint8_t alloc;			//表示字符串的最大容量，不包含Header和最后的空终止字符
	unsigned char flags;	//表示header的类型
	char buf[1];
}sdshdr8;

typedef struct  
{
	uint16_t len;
	uint16_t alloc;
	unsigned char flags;
	char buf[1];
}sdshdr16;

typedef struct  
{
	uint32_t len;
	uint32_t alloc;
	unsigned char flags;
	char buf[1];
}sdshdr32;

typedef struct
{
	uint64_t len;
	uint64_t alloc;
	unsigned char flags;
	char buf[1];
}sdshdr64;

#pragma pack(pop)

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

sds sdsnewlen(const void *init, size_t initlen);

#endif