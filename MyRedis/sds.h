#ifndef __SDS_H
#define __SDS_H

#include "stdint.h"

/*
��������������������������������������������������������������������
|-----header----|------data------|
----------------------------------
|len|alloc|flags|r|e|d|i|s|\0|...|
��������������������������������������������������������������������
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
	uint8_t len;			//��ʾ�ַ��������ĳ��ȣ�����������ֹ�ַ�
	uint8_t alloc;			//��ʾ�ַ��������������������Header�����Ŀ���ֹ�ַ�
	unsigned char flags;	//��ʾheader������
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

//5��header����
#define SDS_TYPE_5	0
#define SDS_TYPE_8	1
#define SDS_TYPE_16 2
#define SDS_TYPE_32 3
#define SDS_TYPE_64 4
#define SDS_TYPE_MASK 7		//��������
#define SDS_TYPE_BITS 3
//##�ǽ������������ӳ�һ������sdshdr��8��TΪ8���ϳ�sdshdr8
#define SDS_HDR_VAR(T, s) sdshdr##T *sh = (sdshdr##T*)((s) - (sizeof(sdshdr##T))); // ��ȡheaderͷָ��
#define SDS_HDR(T, s) ((sdshdr##T *)((s) - (sizeof(sdshdr##T)))) // ��ȡheaderͷָ��
#define SDS_TYPE_5_LEN(f) ((f) >> SDS_TYPE_BITS) // ��ȡsdshdr5�ĳ���

sds sdsnewlen(const void *init, size_t initlen);

#endif