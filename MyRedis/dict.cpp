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

/*通过dictEnableResize() / dictDisableResize()方法我们可以启用/禁用ht空间重新分配.
* 这对于Redis来说很重要, 因为我们用的是写时复制机制而且不想在子进程执行保存操作时移动过多的内存.
*
* 需要注意的是，即使dict_can_resize设置为0, 并不意味着所有的resize操作都被禁止:
* 一个a hash table仍然可以拓展空间，如果bucket与element之间的比例  > dict_force_resize_ratio。
*/
static int dict_can_resize = 1;
static unsigned int dict_force_resize_ratio = 5;

/* -------------------------- 私有原型 ---------------------------- */
static int _dictExpandIfNeeded(dict *ht);
static unsigned long _dictNextPower(unsigned long size);
static int _dictKeyIndex(dict *ht, const void *key);
static int _dictInit(dict *ht, dictType *type, void *privDataPtr);

/* -------------------------- hash方法 ---------------------------- */
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
* Note - 以下代码对你的机器做了一些假设:
* 1. 从任何空间地址读取4byte的数据都不会造成crash
* 2. sizeof(int) == 4
*
* 同时它有一些限制-
*
* 1. 它不是增量运行的.
* 2. 在大端/小端的机器上不会产生相同的结果.
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

/* 重置已经调用过ht_init()来初始化的hash table.
* NOTE: 该方法应当只由 ht_destroy()来调用. */
static int _dictReset(dictht *ht)
{
	ht->table = nullptr;
	ht->size = 0;
	ht->sizemask = 0;
	ht->used = 0;
}

// 创建一个空字典
dict *dictCreate(dictType *type, void *privDataPtr)
{
	dict *d = (dict*)zmalloc(sizeof(dictType));

	_dictInit(d, type, privDataPtr);
	return d;
}

/* 执行N步渐进式的rehash操作，如果仍存在旧表中的数据迁移到新表，则返回1，反之返回0
* 每一步操作移动一个索引值下的键值对到新表 ht[0]->ht[1]
*/
int dictRehash(dict *d, int n)
{
	int empty_visits = n * 10;  // 最大允许访问的空桶值，也就是该索引下没有键值对
	if (!dictIsRehashing(d)) return 0;

	while (n-- &&d->ht[0].used != 0)
	{
		dictEntry *de, *nextde;

		assert(d->ht[0].size > (unsigned long)d->rehashidx);
		// 获取rehash的索引
		while (d->ht[0].table[d->rehashidx] == nullptr)
		{
			d->rehashidx++;
			if (--empty_visits == 0) return 1;
		}
		// 获取需要rehash的索引值下的链表
		de = d->ht[0].table[d->rehashidx];
		// 将该索引下的键值对全部转移到新表
		while (de)
		{
			unsigned int h;
			nextde = de->next;
			// 计算该键值对在新表中的索引
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

	// 键值是否整个表都迁移完成
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

// 在执行查询和更新操作时，如果符合rehash条件就会触发一次rehash操作，每次执行一步
static void _dictRehashStep(dict *d)
{
	if (d->iterators == 0) dictRehash(d, 1);
}

// 向字典添加一个键值对
int dictAdd(dict *d, void *key, void *val)
{
	// 添加一个只有key的键值对
	dictEntry *entry = dictAddRaw(d, key);

	if (!entry) return DICT_ERR;
	// 为添加的只有key键值对设定值
	dictSetVal(d, entry, val);
	return DICT_OK;
}

/* 添加键值对
* 如果key已存在，则返回nullptr
* 如果key成功添加，则返回新的节点
*/
dictEntry *dictAddRaw(dict *d, void *key)
{
	int index;
	dictEntry *entry;
	dictht *ht;
	// 如果正在进行rehash操作，则先执行rehash操作
	if (dictIsRehashing(d)) _dictRehashStep(d);

	// 获取新键值对的索引值，如果key存在则返回-1
	if ((index = _dictKeyIndex(d, key)) == -1)
		return nullptr;

	// 如果正在进行rehash则添加到ht[1]，反之则添加到ht[0]
	ht = dictIsRehashing(d) ? &d->ht[1] : &d->ht[0];
	entry = (dictEntry*)zmalloc(sizeof(dictEntry));
	// 使用开链法来处理哈希冲突
	entry->next = ht->table[index];
	ht->table[index] = entry;
	ht->used++;

	// 设定键的大小
	dictSetKey(d, entry, key);
	return entry;
}

/*另一种添加键值对的方法
* 如果当key不存在时返回1，反之返回0并替换val的值
*/
int dictReplace(dict *d, void *key, void *val)
{
	dictEntry *entry, auxentry;

	// 直接调用dictAdd函数，如果添加成功就表示没有存在相同的key
	if (dictAdd(d, key, val) == DICT_OK)
		return 1;
	// 如果存在相同的key，则先获取该键值对
	entry = dictFind(d, key);
	// 然后用新的value来替换旧value
	auxentry = *entry;
	dictSetVal(d, entry, val);
	dictFreeVal(d, &auxentry);
	return 0;
}

// 查找键值对
dictEntry *dictFind(dict *d, const void *key)
{
	dictEntry *he;
	unsigned int h, idx, table;

	// 空的字典
	if (d->ht[0].used + d->ht[1].used == 0) return nullptr;
	// 如果正在进行rehash，则执行rehash操作
	if (dictIsRehashing(d)) _dictRehashStep(d);
	// 计算哈希值
	h = dictHashKey(d, key);
	// 在两个表中查找对应的键值对
	for (table = 0; table <= 1; table++)
	{
		// 根据掩码来计算索引值
		idx = h & d->ht[table].sizemask;
		// 得到该索引值下的存放的键值对链表
		he = d->ht[table].table[idx];
		while (he) 
		{
			// 如果找到该key直接返回
			if (key == he->key || dictCompareKeys(d, key, he->key))
				return he;
			he = he->next;
		}
		// 如果没有进行rehash，则直接返回
		if (!dictIsRehashing(d)) return nullptr;
	}
	return nullptr;
}

// 返回给定键的值
void *dictFetchValue(dict *d, const void *key)
{
	dictEntry *he;

	he = dictFind(d, key);
	return he ? dictGetVal(he) : nullptr;
}