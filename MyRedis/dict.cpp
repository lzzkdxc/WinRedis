#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#if defined(_WIN32)
#include <WinSock2.h>
#include <time.h>
#else
#include <sys/time.h>
#endif
#include "dict.h"
#include "zmalloc.h"
#include "redisassert.h"

#if defined(_WIN32)
#define random rand
#endif

/*通过dictEnableResize() / dictDisableResize()方法我们可以启用/禁用ht空间重新分配.
* 这对于Redis来说很重要, 因为我们用的是写时复制机制而且不想在子进程执行保存操作时移动过多的内存.
*
* 需要注意的是，即使dict_can_resize设置为0, 并不意味着所有的resize操作都被禁止:
* 一个a hash table仍然可以拓展空间，如果bucket与element之间的比例  > dict_force_resize_ratio
*/
static int dict_can_resize = 1;
static unsigned int dict_force_resize_ratio = 5;

/* -------------------------- 私有原型 ---------------------------- */
static int _dictExpandIfNeeded(dict *d);
static unsigned long _dictNextPower(unsigned long size);
static int _dictKeyIndex(dict *d, const void *key);
static int _dictInit(dict *d, dictType *type, void *privDataPtr);

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

static size_t dict_hash_function_seed = 5381;

void dictSetHashFunctionSeed(size_t seed)
{
	dict_hash_function_seed = seed;
}

size_t dictGetHashFunctionSeed(void)
{
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
unsigned int dictGenHashFunction(const void *key, int len) 
{
	/* 'm' and 'r' are mixing constants generated offline.
	They're not really 'magic', they just happen to work well.  */
	uint32_t seed = dict_hash_function_seed;
	const uint32_t m = 0x5bd1e995;
	const int r = 24;

	/* Initialize the hash to a 'random' value */
	uint32_t h = seed ^ len;

	/* Mix 4 bytes at a time into the hash */
	const unsigned char *data = (const unsigned char *)key;

	while (len >= 4) 
	{
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
	switch (len) 
	{
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
unsigned int dictGenCaseHashFunction(const unsigned char *buf, int len) 
{
	unsigned int hash = (unsigned int)dict_hash_function_seed;

	while (len--)
		hash = ((hash << 5) + hash) + (tolower(*buf++)); /* hash * 33 + c */
	return hash;
}

/* ----------------------------- API implementation ------------------------- */

/* 重置已经调用过ht_init()来初始化的hash table.
* NOTE: 该方法应当只由 ht_destroy()来调用. */
static void _dictReset(dictht *ht)
{
	ht->table = NULL;
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

// 初始化字典
int _dictInit(dict *d, dictType *type, void *privDataPtr)
{
	_dictReset(&d->ht[0]);
	_dictReset(&d->ht[1]);
	d->type = type;
	d->privdata = privDataPtr;
	d->rehashidx = -1;
	d->iterators = 0;
	return DICT_OK;
}

// 调整字典大小，去除多余的长度
int dictResize(dict *d)
{
	int minimal;

	if (!dict_can_resize || dictIsRehashing(d)) return DICT_ERR;
	minimal = d->ht[0].used;
	if (minimal < DICT_HT_INITIAL_SIZE)
		minimal = DICT_HT_INITIAL_SIZE;
	return dictExpand(d, minimal);
}

// 扩展或创建哈希表
int dictExpand(dict *d, unsigned long size)
{
	dictht ht;
	unsigned long realsize = _dictNextPower(size);
	// 如果正在rehash或者hash的数量小于已使用的数量，那这个值无效
	if (dictIsRehashing(d) || d->ht[0].used > size) return DICT_ERR;
	// rehash相同数量的哈希表数量没什么用
	if (realsize == d->ht[0].size) return DICT_ERR;
	// 分配新的哈希表并初始化
	ht.size = realsize;
	ht.sizemask = realsize - 1;
	ht.table = (dictEntry**)zcalloc(realsize*sizeof(dictEntry*));
	ht.used = 0;
	// 如果是第一次初始化，我们只需要设置第一个哈希表以便可以接收键值
	if (d->ht[0].table == NULL)
	{
		d->ht[0] = ht;
		return DICT_OK;
	}

	d->ht[1] = ht;
	d->rehashidx = 0;
	return DICT_OK;
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
		while (d->ht[0].table[d->rehashidx] == NULL)
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
		d->ht[0].table[d->rehashidx] = NULL;
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

long long timeInMilliseconds()
{
	struct timeval tv;
#if defined(_WIN32)
	time_t clock;
	struct tm t;
	SYSTEMTIME wtm;

	GetLocalTime(&wtm);
	t.tm_year = wtm.wYear - 1900;
	t.tm_mon = wtm.wMonth - 1;
	t.tm_mday = wtm.wDay;
	t.tm_hour = wtm.wHour;
	t.tm_min = wtm.wMinute;
	t.tm_sec = wtm.wSecond;
	t.tm_isdst = -1;
	clock = mktime(&t);
	tv.tv_sec = (long)clock;
	tv.tv_usec = wtm.wMilliseconds * 1000;
#else
	gettimeofday(&tv, NULL);
#endif
	return (((long long)tv.tv_sec) * 1000) + (tv.tv_usec / 1000);
}

int dictRehashMilliseconds(dict *d, int ms)
{
	auto start = timeInMilliseconds();
	int rehashes = 0;

	while (dictRehash(d, 100))
	{
		rehashes += 100;
		if (timeInMilliseconds() - start > ms) break;
	}
	return rehashes;
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
* 如果key已存在，则返回NULL
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
		return NULL;

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

dictEntry *dictReplaceRaw(dict *d, void *key)
{
	dictEntry *entry = dictFind(d, key);
	return entry ? entry : dictAddRaw(d, key);
}

// 查找键值对
dictEntry *dictFind(dict *d, const void *key)
{
	dictEntry *he;
	unsigned int h, idx;

	// 空的字典
	if (d->ht[0].used + d->ht[1].used == 0) return NULL;
	// 如果正在进行rehash，则执行rehash操作
	if (dictIsRehashing(d)) _dictRehashStep(d);
	h = dictHashKey(d, key);
	// 在两个表中查找对应的键值对
	for (int table = 0; table <= 1; table++)
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
		if (!dictIsRehashing(d)) return NULL;
	}
	return NULL;
}

// 返回给定键的值
void *dictFetchValue(dict *d, const void *key)
{
	dictEntry *he;

	he = dictFind(d, key);
	return he ? dictGetVal(he) : NULL;
}

// 从字典中随机返回一个键值对
dictEntry *dictGetRandomKey(dict *d)
{
	dictEntry *he, *orighe;
	unsigned int idx;
	int listlen, listele;

	if (dictSize(d) == 0) return NULL;
	// 如果正在进行rehash，则执行一次rehash操作
	if (dictIsRehashing(d)) _dictRehashStep(d);
	// 随机返回一个键的具体操作是：先随机选取一个索引值，然后在该索引值
	// 对应的键值对链表中随机选取一个键值对返回
	if (dictIsRehashing(d))
	{
		// 如果正在rehash则需要考虑两个哈希表中的数据
		do
		{
			// 我们确定在索引0到rehashidx-1之间没有元素
			idx = d->rehashidx + (random() % (d->ht[0].size + d->ht[1].size - d->rehashidx));
			he = (idx >= d->ht[0].size) ? d->ht[1].table[idx - d->ht[0].size] : d->ht[0].table[idx];
		} while (he == NULL);
	}
	else
	{
		do 
		{
			idx = random() & d->ht[0].sizemask;
			he = d->ht[0].table[idx];
		} while (he == NULL);
	}

	// 这里获得一个非空的桶，先统计队列中节点的数量，再随机一个下标
	listlen = 0;
	orighe = he;
	while (he)
	{
		he = he->next;
		listlen++;
	}
	listele = random() % listlen;
	he = orighe;
	while (listele--) he = he->next;
	return he;
}

// 随机返回多个键，并存储在des中, 返回实际的键数量
size_t dictGetSomeKeys(dict *d, dictEntry **des, size_t count)
{
	unsigned long j;
	unsigned long tables;
	unsigned long stored = 0, maxsizemask;
	unsigned long maxsteps;

	if (dictSize(d) < count) count = dictSize(d);
	maxsteps = count * 10;

	// 尝试count次rehash
	for (j = 0; j < count; j++)
	{
		if (dictIsRehashing(d))
			_dictRehashStep(d);
		else
			break;
	}

	tables = dictIsRehashing(d) ? 2 : 1;
	maxsizemask = d->ht[0].sizemask;
	if (tables > 1 && maxsizemask < d->ht[1].sizemask)
		maxsizemask = d->ht[1].sizemask;

	unsigned long i = random() & maxsizemask;
	unsigned long emptylen = 0;
	while (stored < count && maxsteps--)
	{
		for (j = 0; j < tables; j++)
		{
			if (tables == 2 && j == 0 && i < (unsigned long)d->rehashidx)
			{
				if (i >= d->ht[1].size)
					i = d->rehashidx;
				continue;
			}

			if (i >= d->ht[1].size) continue;
			dictEntry *he = d->ht[j].table[i];

			// 统计连续的空桶，如果次数在5次以上并且达到count的次数，则跳转到其他地方
			if (he == NULL)
			{
				emptylen++;
				if (emptylen >= 5 && emptylen > count)
				{
					i = random() & maxsizemask;
					emptylen = 0;
				}
			}
			else
			{
				emptylen = 0;
				while (he)
				{
					// 收集所有非空的元素
					*des = he;
					des++;
					he = he->next;
					stored++;
					if (stored == count) return stored;
				}
			}
		}
		i = (i + 1) & maxsizemask;
	}
	return stored;
}

/* 查找并删除指定键对应的键值对
* nofree: 是否释放键和值
*/
static int dictGenericDelete(dict *d, const void *key, int nofree)
{
	unsigned int h, idx;
	dictEntry *he, *prevHe;

	if (d->ht[0].size == 0) return DICT_ERR;
	if (dictIsRehashing(d)) _dictRehashStep(d);
	h = dictHashKey(d, key);

	for (int table = 0; table <= 1; table++)
	{
		// 计算索引值
		idx = h & d->ht[table].sizemask;
		he = d->ht[table].table[idx];
		prevHe = NULL;
		// 执行在链表中删除某个节点的操作
		while (he)
		{
			if (key == he->key || dictCompareKeys(d, key, he->key))
			{
				// 从列表中取消链接元素
				if (prevHe)
					prevHe->next = he->next;
				else
					d->ht[table].table[idx] = he->next;
				if (!nofree)
				{
					// 释放键和值
					dictFreeKey(d, he);
					dictFreeVal(d, he);
				}
				zfree(he);
				d->ht[table].used--;
				return DICT_OK;
			}
			prevHe = he;
			he = he->next;
		}
		// 如果没有进行rehash操作，则没必要对ht[1]进行查找
		if (!dictIsRehashing(d)) break;
	}
	return DICT_ERR;
}

// 删除该键值对，并释放键和值
int dictDelete(dict *d, const void *key)
{
	return dictGenericDelete(d, key, 0);
}

// 删除该键值对，不释放键和值
int dictDeleteNoFree(dict *d, const void *key)
{
	return dictGenericDelete(d, key, 1);
}

// 释放哈希表
int _dictClear(dict *d, dictht *ht, void (callback)(void*))
{
	// 清除和释放所有元素
	for (auto i = 0ul; i < ht->size && ht->used > 0; i++)
	{
		dictEntry *he, *nextHe;

		if (callback && (i & 65535) == 0) callback(d->privdata);
		if ((he = ht->table[i]) == NULL) continue;

		while (he)
		{
			nextHe = he->next;
			dictFreeKey(d, he);
			dictFreeVal(d, he);
			zfree(he);
			ht->used--;
			he = nextHe;
		}
	}
	// 释放哈希表
	zfree(ht->table);
	// 重置哈希表
	_dictReset(ht);
	return DICT_OK;
}

// 删除和释放整个字典结构
void dictRelease(dict *d)
{
	_dictClear(d, &d->ht[0], NULL);
	_dictClear(d, &d->ht[1], NULL);
	zfree(d); // 释放字典
}

// 如果需要，则扩展哈希表
static int _dictExpandIfNeeded(dict *d)
{
	// rehash正在进行，直接返回
	if (dictIsRehashing(d)) return DICT_OK;
	// 如果哈希表是空的，以初始长度来创建
	if (d->ht[0].size == 0) return dictExpand(d, DICT_HT_INITIAL_SIZE);
	/* 如果我们达到1：1的比例，并且我们被允许可以调整大小或者元素和桶之间的比例超过了安全阀值，
	那么我们把桶的大小调整成原来的两倍*/
	if (d->ht[0].used >= d->ht[0].size &&
		(dict_can_resize || d->ht[0].used / d->ht[0].size > dict_force_resize_ratio))
	{
		return dictExpand(d, d->ht[0].used * 2);
	}
	return DICT_OK;
}

// 扩展哈希表容量，每次扩展之前大小的两倍，返回扩展后的大小
static unsigned long _dictNextPower(unsigned long size)
{
	unsigned long i = DICT_HT_INITIAL_SIZE;

	if (size > LONG_MAX) return LONG_MAX;
	while (1)
	{
		if (i >= size)
			return i;
		i *= 2;
	}
}

// 根据key计算在空哈希表中的索引，如果key已存在，返回-1
static int _dictKeyIndex(dict *d, const void *key)
{
	unsigned int h, idx;
	dictEntry *he;

	if (_dictExpandIfNeeded(d) == DICT_ERR)
		return -1;

	h = dictHashKey(d, key);
	for (auto table = 0; table <= 1; table++)
	{
		idx = h & d->ht[table].sizemask;
		he = d->ht[table].table[idx];
		while (he)
		{
			if (key == he->key || dictCompareKeys(d, key, he->key))
				return -1;
			he = he->next;
		}
		if (!dictIsRehashing(d)) break;
	}
	return idx;
}

// 清空字典表
void dictEmpty(dict *d, void(callback)(void*))
{
	_dictClear(d, &d->ht[0], callback);
	_dictClear(d, &d->ht[1], callback);
	d->rehashidx = -1;
	d->iterators = 0;
}

void dictEnableResize(void)
{
	dict_can_resize = 1;
}

void dictDisableResize(void)
{
	dict_can_resize = 0;
}
