#ifndef __REDIS_H
#define __REDIS_H

#pragma warning(disable:4200)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "sds.h"
#include "dict.h"
#include "zmalloc.h"

/* �������� */
#define OBJ_STRING 0
#define OBJ_LIST 1
#define OBJ_SET 2
#define OBJ_ZSET 3
#define OBJ_HASH 4

/* ������� */
#define OBJ_ENCODING_RAW 0     /* Raw representation */
#define OBJ_ENCODING_INT 1     /* Encoded as integer */
#define OBJ_ENCODING_HT 2      /* Encoded as hash table */
#define OBJ_ENCODING_ZIPMAP 3  /* Encoded as zipmap */
#define OBJ_ENCODING_LINKEDLIST 4 /* Encoded as regular linked list */
#define OBJ_ENCODING_ZIPLIST 5 /* Encoded as ziplist */
#define OBJ_ENCODING_INTSET 6  /* Encoded as intset */
#define OBJ_ENCODING_SKIPLIST 7  /* Encoded as skiplist */
#define OBJ_ENCODING_EMBSTR 8  /* Embedded sds string encoding */
#define OBJ_ENCODING_QUICKLIST 9 /* Encoded as linked list of ziplists */

#define ZSKIPLIST_MAXLEVEL 32
#define ZSKIPLIST_P 0.25

/* ��ӡ��ջ�������� */
#define serverAssertWithInfo(_c, _o, _e) ((_e)?(void)0:(_serverAssertWithInfo(_c,_o,#_e,__FILE__,__LINE__), _exit(1)))
#define serverAssert(_e) ((_e)?(void)0 : (_serverAssert(#_e, __FILE__, __LINE__), _exit(1)))
#define serverPanic(_e) _serverPanic(#_e, __FILE__,__LINE__),_exit(1)

#define LRU_BITS 24
#define LRU_CLOCK_MAX ((1<<LRU_BITS)-1)
#define LRU_CLOCK_RESOLUTION 1000
typedef struct redisObject 
{
	unsigned type : 4;
	unsigned encoding : 4;
	unsigned lru : LRU_BITS;
	int refcount;
	void *ptr;
} robj;

typedef struct client
{

} client;

typedef struct zskiplistNode 
{
	robj *obj; //��Ա����
	double score; //��ֵ
	struct zskiplistNode *backward; //����ָ��
	struct zskiplistLevel 
	{
		struct zskiplistNode *forward; //ǰ��ָ��
		unsigned int span; //���
	} level[]; //��ʼ��һ����Ծ��ڵ��ʱ���Ϊ���������һ�����С��ÿ���ڵ��ÿһ�����������ʽ��������
} zskiplistNode;

typedef struct zskiplist
{
	//��Ծ��ı�ͷ�ڵ�ͱ�β�ڵ�
	struct zskiplistNode *header, *tail;
	//���нڵ������
	unsigned long length;
	//���в������Ľڵ����
	int level;
} zskiplist;

typedef struct zset 
{
	dict *dt;
	zskiplist *zsl;
} zset;

/* Redis����ʵ�� */
void decrRefCount(robj *o);
void freeStringObject(robj *o);
void freeListObject(robj *o);
void freeSetObject(robj *o);
void freeZsetObject(robj *o);
void freeHashObject(robj *o);

int compareStringObjects(robj *a, robj *b);
int collateStringObjects(robj *a, robj *b);
int equalStringObjects(robj *a, robj *b);
#define sdsEncodedObject(objptr) (objptr->encoding == OBJ_ENCODING_RAW || objptr->encoding == OBJ_ENCODING_EMBSTR)

zskiplist *zslCreate(void);
void zslFree(zskiplist *zsl);

/* ���Ժ��� */
void _serverAssertWithInfo(client *c, robj *o, char *estr, char *file, int line);
void _serverAssert(char *estr, char *file, int line);
void _serverPanic(char *msg, char *file, int line);
zskiplistNode *zslInsert(zskiplist *zsl, double score, robj *obj);



#endif