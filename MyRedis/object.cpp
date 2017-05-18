#include "server.h"
#include <math.h>
#include <ctype.h>

void freeStringObject(robj *o)
{
	if (o->encoding == OBJ_ENCODING_RAW)
	{
		sdsfree((sds)o->ptr);
	}
}

void freeListObject(robj *o)
{
	if (o->encoding == OBJ_ENCODING_QUICKLIST)
		;//quicklistRelease(o->ptr);
	else
		serverPanic("Unknown list encoding type");
}

void freeSetObject(robj *o)
{
	switch (o->encoding)
	{
	case OBJ_ENCODING_HT:
		dictRelease((dict*)o->ptr);
	case OBJ_ENCODING_INTSET:
		zfree(o->ptr);
		break;
	default:
		serverPanic("Unknown set encoding type");
		break;
	}
}

void freeZsetObject(robj *o)
{
	zset *zs;
	switch (o->encoding)
	{
	case OBJ_ENCODING_SKIPLIST:
		zs = (zset*)o->ptr;
		dictRelease(zs->dt);
		zslFree(zs->zsl);
		zfree(zs);
		break;
	case OBJ_ENCODING_ZIPLIST:
		zfree(o->ptr);
		break;
	default:
		serverPanic("Unknown sorted set encoding");
		break;
	}

}

void freeHashObject(robj *o)
{
	switch (o->encoding)
	{
	case OBJ_ENCODING_HT:
		dictRelease((dict*)o->ptr);
		break;
	case OBJ_ENCODING_ZIPLIST:
		zfree(o->ptr);
		break;
	default:
		serverPanic("Unknown hash encoding type");
		break;
	}
}

void decrRefCount(robj *o)
{
	if (o->refcount <= 0) serverPanic("decrRefCount against refcount <= 0");
	if (o->refcount == 1)
	{
		switch (o->type)
		{
		case OBJ_STRING: freeStringObject(o); break;
		case OBJ_LIST: freeListObject(o); break;
		case OBJ_SET: freeSetObject(o); break;
		case OBJ_ZSET: freeZsetObject(o); break;
		case OBJ_HASH: freeHashObject(o); break;
		default: serverPanic("Unknown object type"); break;
		}
		zfree(o);
	}
	else
	{
		o->refcount--;
	}
}

#define REDIS_COMPARE_BINARY (1<<0)
#define REDIS_COMPARE_COLL (1<<1)

int compareStringObjectsWithFlags(robj *a, robj *b, int flags)
{
	serverAssertWithInfo(NULL, a, a->type == OBJ_STRING && b->type == OBJ_STRING);
	char bufa[128], bufb[128], *astr, *bstr;
	size_t alen, blen, minlen;

	if (a == b) return 0;
	if (sdsEncodedObject(a))
	{
		astr = (char*)a->ptr;
		alen = sdslen(astr);
	}
	else
	{
		//alen = ll2string(bufa, sizeof(bufa), (long)a->ptr);
	}
	return 0;
}

/* 使用二进制进行比较 */
int compareStringObjects(robj *a, robj *b)
{
	return compareStringObjectsWithFlags(a, b, REDIS_COMPARE_BINARY);
}

/* 使用排序规则进行比较 */
int collateStringObjects(robj *a, robj *b)
{
	return compareStringObjectsWithFlags(a, b, REDIS_COMPARE_COLL);
}

/* 如果两个对象从字符串比较的角度相同返回1，否则返回0 */
int equalStringObjects(robj *a, robj *b)
{
	if (a->encoding == OBJ_ENCODING_INT && b->encoding == OBJ_ENCODING_INT)
	{
		return a->ptr == b->ptr;
	}
	else
	{
		return compareStringObjects(a, b) == 0;
	}
}