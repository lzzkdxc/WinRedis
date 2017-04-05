#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#if defined(_WIN32)

#else
#include <sys/time.h>
#endif
#include <ctype.h>
#include "dict.h"
#include "zmalloc.h"
#include "redisassert.h"

/*ͨ��dictEnableResize() / dictDisableResize()�������ǿ�������/����ht�ռ����·���.
* �����Redis��˵����Ҫ, ��Ϊ�����õ���дʱ���ƻ��ƶ��Ҳ������ӽ���ִ�б������ʱ�ƶ�������ڴ�.
*
* ��Ҫע����ǣ���ʹdict_can_resize����Ϊ0, ������ζ�����е�resize����������ֹ:
* һ��a hash table��Ȼ������չ�ռ䣬���bucket��element֮��ı���  > dict_force_resize_ratio��
*/
static int dict_can_resize = 1;
static unsigned int dict_force_resize_ratio = 5;

/* -------------------------- ˽��ԭ�� ---------------------------- */
static int _dictExpandIfNeeded(dict *ht);
static unsigned long _dictNextPower(unsigned long size);
static int _dictKeyIndex(dict *ht, const void *key);
static int _dictInit(dict *ht, dictType *type, void *privDataPtr);

/* -------------------------- hash���� ---------------------------- */
/* Thomas Wang's 32 bit Mix Function */
unsigned int dictIntHashFunction(unsigned int key)
{
	key += ~(key << 15);
	key ^= (key >> 10);
	key += (key << 3);
	key ^= (key >> 6);
	key += ~(key << 11);
	key ^= (key >> 16);
	return key;
}

static uint32_t dict_hash_function_seed = 5381;

void dictSetHashFunctionSeed(uint32_t seed) {
	dict_hash_function_seed = seed;
}

uint32_t dictGetHashFunctionSeed(void) {
	return dict_hash_function_seed;
}

/* MurmurHash2, by Austin Appleby
* Note - ���´������Ļ�������һЩ����:
* 1. ���κοռ��ַ��ȡ4byte�����ݶ��������crash
* 2. sizeof(int) == 4
*
* ͬʱ����һЩ����-
*
* 1. �������������е�.
* 2. �ڴ��/С�˵Ļ����ϲ��������ͬ�Ľ��.
*/
unsigned int dictGenHashFunction(const void *key, int len) {
	/* 'm' and 'r' are mixing constants generated offline.
	They're not really 'magic', they just happen to work well.  */
	uint32_t seed = dict_hash_function_seed;
	const uint32_t m = 0x5bd1e995;
	const int r = 24;

	/* Initialize the hash to a 'random' value */
	uint32_t h = seed ^ len;

	/* Mix 4 bytes at a time into the hash */
	const unsigned char *data = (const unsigned char *)key;

	while (len >= 4) {
		uint32_t k = *(uint32_t*)data;

		k *= m;
		k ^= k >> r;
		k *= m;

		h *= m;
		h ^= k;

		data += 4;
		len -= 4;
	}

	/* Handle the last few bytes of the input array  */
	switch (len) {
	case 3: h ^= data[2] << 16;
	case 2: h ^= data[1] << 8;
	case 1: h ^= data[0]; h *= m;
	};

	/* Do a few final mixes of the hash to ensure the last few
	* bytes are well-incorporated. */
	h ^= h >> 13;
	h *= m;
	h ^= h >> 15;

	return (unsigned int)h;
}

/* And a case insensitive hash function (based on djb hash) */
unsigned int dictGenCaseHashFunction(const unsigned char *buf, int len) {
	unsigned int hash = (unsigned int)dict_hash_function_seed;

	while (len--)
		hash = ((hash << 5) + hash) + (tolower(*buf++)); /* hash * 33 + c */
	return hash;
}

/* ----------------------------- API implementation ------------------------- */

/* �����Ѿ����ù�ht_init()����ʼ����hash table.
* NOTE: �÷���Ӧ��ֻ�� ht_destroy()������. */
static int _dictReset(dictht *ht)
{
	ht->table = nullptr;
	ht->size = 0;
	ht->sizemask = 0;
	ht->used = 0;
}

// ����һ�����ֵ�
dict *dictCreate(dictType *type, void *privDataPtr)
{
	dict *d = (dict*)zmalloc(sizeof(dictType));

	_dictInit(d, type, privDataPtr);
	return d;
}

/* ִ��N������ʽ��rehash����������Դ��ھɱ��е�����Ǩ�Ƶ��±��򷵻�1����֮����0
* ÿһ�������ƶ�һ������ֵ�µļ�ֵ�Ե��±� ht[0]->ht[1]
*/
int dictRehash(dict *d, int n)
{
	int empty_visits = n * 10;  // ���������ʵĿ�Ͱֵ��Ҳ���Ǹ�������û�м�ֵ��
	if (!dictIsRehashing(d)) return 0;

	while (n-- &&d->ht[0].used != 0)
	{
		dictEntry *de, *nextde;

		assert(d->ht[0].size > (unsigned long)d->rehashidx);
		// ��ȡrehash������
		while (d->ht[0].table[d->rehashidx] == nullptr)
		{
			d->rehashidx++;
			if (--empty_visits == 0) return 1;
		}
		// ��ȡ��Ҫrehash������ֵ�µ�����
		de = d->ht[0].table[d->rehashidx];
		// ���������µļ�ֵ��ȫ��ת�Ƶ��±�
		while (de)
		{
			unsigned int h;
			nextde = de->next;
			// ����ü�ֵ�����±��е�����
			h = dictHashKey(d, de->key) & d->ht[1].sizemask;
			de->next = d->ht[1].table[h];
			d->ht[1].table[n] = de;
			d->ht[0].used--;
			d->ht[1].used++;
			de = nextde;
		}
		d->ht[0].table[d->rehashidx] = nullptr;
		d->rehashidx++;
	}

	// ��ֵ�Ƿ�������Ǩ�����
	if (d->ht[0].used == 0)
	{
		zfree(d->ht[0].table);
		d->ht[0] = d->ht[1];
		_dictReset(&d->ht[1]);
		d->rehashidx = -1;
		return 0;
	}

	return 1;
}

// ��ִ�в�ѯ�͸��²���ʱ���������rehash�����ͻᴥ��һ��rehash������ÿ��ִ��һ��
static void _dictRehashStep(dict *d)
{
	if (d->iterators == 0) dictRehash(d, 1);
}

// ���ֵ����һ����ֵ��
int dictAdd(dict *d, void *key, void *val)
{
	// ���һ��ֻ��key�ļ�ֵ��
	dictEntry *entry = dictAddRaw(d, key);

	if (!entry) return DICT_ERR;
	// Ϊ��ӵ�ֻ��key��ֵ���趨ֵ
	dictSetVal(d, entry, val);
	return DICT_OK;
}

/* ��Ӽ�ֵ��
* ���key�Ѵ��ڣ��򷵻�nullptr
* ���key�ɹ���ӣ��򷵻��µĽڵ�
*/
dictEntry *dictAddRaw(dict *d, void *key)
{
	int index;
	dictEntry *entry;
	dictht *ht;
	// ������ڽ���rehash����������ִ��rehash����
	if (dictIsRehashing(d)) _dictRehashStep(d);

	// ��ȡ�¼�ֵ�Ե�����ֵ�����key�����򷵻�-1
	if ((index = _dictKeyIndex(d, key)) == -1)
		return nullptr;

	// ������ڽ���rehash����ӵ�ht[1]����֮����ӵ�ht[0]
	ht = dictIsRehashing(d) ? &d->ht[1] : &d->ht[0];
	entry = (dictEntry*)zmalloc(sizeof(dictEntry));
	// ʹ�ÿ������������ϣ��ͻ
	entry->next = ht->table[index];
	ht->table[index] = entry;
	ht->used++;

	// �趨���Ĵ�С
	dictSetKey(d, entry, key);
	return entry;
}

/*��һ����Ӽ�ֵ�Եķ���
* �����key������ʱ����1����֮����0���滻val��ֵ
*/
int dictReplace(dict *d, void *key, void *val)
{
	dictEntry *entry, auxentry;

	// ֱ�ӵ���dictAdd�����������ӳɹ��ͱ�ʾû�д�����ͬ��key
	if (dictAdd(d, key, val) == DICT_OK)
		return 1;
	// ���������ͬ��key�����Ȼ�ȡ�ü�ֵ��
	entry = dictFind(d, key);
	// Ȼ�����µ�value���滻��value
	auxentry = *entry;
	dictSetVal(d, entry, val);
	dictFreeVal(d, &auxentry);
	return 0;
}

// ���Ҽ�ֵ��
dictEntry *dictFind(dict *d, const void *key)
{
	dictEntry *he;
	unsigned int h, idx, table;

	// �յ��ֵ�
	if (d->ht[0].used + d->ht[1].used == 0) return nullptr;
	// ������ڽ���rehash����ִ��rehash����
	if (dictIsRehashing(d)) _dictRehashStep(d);
	// �����ϣֵ
	h = dictHashKey(d, key);
	// ���������в��Ҷ�Ӧ�ļ�ֵ��
	for (table = 0; table <= 1; table++)
	{
		// ������������������ֵ
		idx = h & d->ht[table].sizemask;
		// �õ�������ֵ�µĴ�ŵļ�ֵ������
		he = d->ht[table].table[idx];
		while (he) 
		{
			// ����ҵ���keyֱ�ӷ���
			if (key == he->key || dictCompareKeys(d, key, he->key))
				return he;
			he = he->next;
		}
		// ���û�н���rehash����ֱ�ӷ���
		if (!dictIsRehashing(d)) return nullptr;
	}
	return nullptr;
}

// ���ظ�������ֵ
void *dictFetchValue(dict *d, const void *key)
{
	dictEntry *he;

	he = dictFind(d, key);
	return he ? dictGetVal(he) : nullptr;
}