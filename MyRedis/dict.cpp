#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#if defined(_WIN32)
#include <time.h>
#else
#include <sys/time.h>
#endif
#include <ctype.h>
#include "dict.h"
#include "zmalloc.h"
#include "redisassert.h"

#if defined(_WIN32)
#define random rand
#endif

/*ͨ��dictEnableResize() / dictDisableResize()�������ǿ�������/����ht�ռ����·���.
* �����Redis��˵����Ҫ, ��Ϊ�����õ���дʱ���ƻ��ƶ��Ҳ������ӽ���ִ�б������ʱ�ƶ�������ڴ�.
*
* ��Ҫע����ǣ���ʹdict_can_resize����Ϊ0, ������ζ�����е�resize����������ֹ:
* һ��a hash table��Ȼ������չ�ռ䣬���bucket��element֮��ı���  > dict_force_resize_ratio��
*/
static int dict_can_resize = 1;
static unsigned int dict_force_resize_ratio = 5;

/* -------------------------- ˽��ԭ�� ---------------------------- */
static int _dictExpandIfNeeded(dict *d);
static unsigned long _dictNextPower(unsigned long size);
static int _dictKeyIndex(dict *d, const void *key);
static int _dictInit(dict *d, dictType *type, void *privDataPtr);

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
static void _dictReset(dictht *ht)
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

// ��ʼ���ֵ�
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

// �����ֵ��С��ȥ������ĳ���
int dictResize(dict *d)
{
	int minimal;

	if (!dict_can_resize || dictIsRehashing(d)) return DICT_ERR;
	minimal = d->ht[0].used;
	if (minimal < DICT_HT_INITIAL_SIZE)
		minimal = DICT_HT_INITIAL_SIZE;
	return dictExpand(d, minimal);
}

// ��չ�򴴽���ϣ��
int dictExpand(dict *d, unsigned long size)
{
	dictht ht;
	unsigned long realsize = _dictNextPower(size);
	// �������rehash����hash������С����ʹ�õ������������ֵ��Ч
	if (dictIsRehashing(d) || d->ht[0].used > size) return DICT_ERR;
	// rehash��ͬ�����Ĺ�ϣ������ûʲô��
	if (realsize == d->ht[0].size) return DICT_ERR;
	// �����µĹ�ϣ����ʼ��
	ht.size = realsize;
	ht.sizemask = realsize - 1;
	ht.table = (dictEntry**)zcalloc(realsize*sizeof(dictEntry*));
	ht.used = 0;
	// ����ǵ�һ�γ�ʼ��������ֻ��Ҫ���õ�һ����ϣ���Ա���Խ��ռ�ֵ
	if (d->ht[0].table == nullptr)
	{
		d->ht[0] = ht;
		return DICT_OK;
	}

	d->ht[1] = ht;
	d->rehashidx = 0;
	return DICT_OK;
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

long long timeInMilliseconds()
{
#if defined(_WIN32)
	return 0;
#else
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return (((long long)tv.tv_sec) * 1000) + (tv.tv_usec / 1000);
#endif
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

dictEntry *dictReplaceRaw(dict *d, void *key)
{
	dictEntry *entry = dictFind(d, key);
	return entry ? entry : dictAddRaw(d, key);
}

// ���Ҽ�ֵ��
dictEntry *dictFind(dict *d, const void *key)
{
	dictEntry *he;
	unsigned int h, idx;

	// �յ��ֵ�
	if (d->ht[0].used + d->ht[1].used == 0) return nullptr;
	// ������ڽ���rehash����ִ��rehash����
	if (dictIsRehashing(d)) _dictRehashStep(d);
	h = dictHashKey(d, key);
	// ���������в��Ҷ�Ӧ�ļ�ֵ��
	for (int table = 0; table <= 1; table++)
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

// ���ֵ����������һ����ֵ��
dictEntry *dictGetRandomKey(dict *d)
{
	dictEntry *he, *orighe;
	unsigned int idx;
	int listlen, listele;

	if (dictSize(d) == 0) return nullptr;
	// ������ڽ���rehash����ִ��һ��rehash����
	if (dictIsRehashing(d)) _dictRehashStep(d);
	// �������һ�����ľ�������ǣ������ѡȡһ������ֵ��Ȼ���ڸ�����ֵ
	// ��Ӧ�ļ�ֵ�����������ѡȡһ����ֵ�Է���
	if (dictIsRehashing(d))
	{
		// �������rehash����Ҫ����������ϣ���е�����
		do
		{
			// ����ȷ��������0��rehashidx-1֮��û��Ԫ��
			idx = d->rehashidx + (random() % (d->ht[0].size + d->ht[1].size - d->rehashidx));
			he = (idx >= d->ht[0].size) ? d->ht[1].table[idx - d->ht[0].size] : d->ht[0].table[idx];
		} while (he == nullptr);
	}
	else
	{
		do 
		{
			idx = random() & d->ht[0].sizemask;
			he = d->ht[0].table[idx];
		} while (he == nullptr);
	}

	// ������һ���ǿյ�Ͱ����ͳ�ƶ����нڵ�������������һ���±�
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

// ������ض���������洢��des��, ����ʵ�ʵļ�����
unsigned int dictGetSomeKeys(dict *d, dictEntry **des, unsigned int count)
{
	unsigned long j;
	unsigned long tables;
	unsigned long stored = 0, maxsizemask;
	unsigned long maxsteps;

	if (dictSize(d) < count) count = dictSize(d);
	maxsteps = count * 10;

	// ����count��rehash
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

			// ͳ�������Ŀ�Ͱ�����������5�����ϲ��Ҵﵽcount�Ĵ���������ת�������ط�
			if (he == nullptr)
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
					// �ռ����зǿյ�Ԫ��
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

/* ���Ҳ�ɾ��ָ������Ӧ�ļ�ֵ��
* nofree: �Ƿ��ͷż���ֵ
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
		// ��������ֵ
		idx = h & d->ht[table].sizemask;
		he = d->ht[table].table[idx];
		prevHe = nullptr;
		// ִ����������ɾ��ĳ���ڵ�Ĳ���
		while (he)
		{
			if (key == he->key || dictCompareKeys(d, key, he->key))
			{
				// ���б���ȡ������Ԫ��
				if (prevHe)
					prevHe->next = he->next;
				else
					d->ht[table].table[idx] = he->next;
				if (!nofree)
				{
					// �ͷż���ֵ
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
		// ���û�н���rehash��������û��Ҫ��ht[1]���в���
		if (!dictIsRehashing(d)) break;
	}
	return DICT_ERR;
}

// ɾ���ü�ֵ�ԣ����ͷż���ֵ
int dictDelete(dict *d, const void *key)
{
	return dictGenericDelete(d, key, 0);
}

// ɾ���ü�ֵ�ԣ����ͷż���ֵ
int dictDeleteNoFree(dict *d, const void *key)
{
	return dictGenericDelete(d, key, 1);
}

// �ͷŹ�ϣ��
int _dictClear(dict *d, dictht *ht, void (callback)(void*))
{
	// ������ͷ�����Ԫ��
	for (auto i = 0; i < ht->size && ht->used > 0; i++)
	{
		dictEntry *he, *nextHe;

		if (callback && (i & 65535) == 0) callback(d->privdata);
		if ((he = ht->table[i]) == nullptr) continue;

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
	// �ͷŹ�ϣ��
	zfree(ht->table);
	// ���ù�ϣ��
	_dictReset(ht);
	return DICT_OK;
}

// ɾ�����ͷ������ֵ�ṹ
void dictRelease(dict *d)
{
	_dictClear(d, &d->ht[0], nullptr);
	_dictClear(d, &d->ht[1], nullptr);
	zfree(d); // �ͷ��ֵ�
}

// �����Ҫ������չ��ϣ��
static int _dictExpandIfNeeded(dict *d)
{
	// rehash���ڽ��У�ֱ�ӷ���
	if (dictIsRehashing(d)) return DICT_OK;
	// �����ϣ���ǿյģ��Գ�ʼ����������
	if (d->ht[0].size == 0) return dictExpand(d, DICT_HT_INITIAL_SIZE);
	/* ������Ǵﵽ1��1�ı������������Ǳ�������Ե�����С����Ԫ�غ�Ͱ֮��ı��������˰�ȫ��ֵ��
	��ô���ǰ�Ͱ�Ĵ�С������ԭ��������*/
	if (d->ht[0].used >= d->ht[0].size &&
		(dict_can_resize || d->ht[0].used / d->ht[0].size > dict_force_resize_ratio))
	{
		return dictExpand(d, d->ht[0].used * 2);
	}
	return DICT_OK;
}

// ��չ��ϣ��������ÿ����չ֮ǰ��С��������������չ��Ĵ�С
static unsigned long _dictNextPower(unsigned long size)
{
	auto i = DICT_HT_INITIAL_SIZE;

	if (size > LONG_MAX) return LONG_MAX;
	while (true)
	{
		if (i >= size)
			return i;
		i *= 2;
	}
}

// ����key�����ڿչ�ϣ���е����������key�Ѵ��ڣ�����-1
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

// ����ֵ��
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