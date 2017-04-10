#ifndef __DICT_H
#define __DICT_H

#include <stdint.h>

#define DICT_OK 0
#define DICT_ERR 1

// 哈希表节点
typedef struct _dictEntry 
{
	void *key;	// 键
	union {
		void *val;
		uint64_t u64;
		int64_t s64;
		double d;
	} v;	// 值
	struct _dictEntry *next; // 指向下一个哈希节点，使用拉链法解决哈希冲突
} dictEntry;

// 字典类型
typedef struct _dictType 
{
	unsigned int (*hashFunction)(const void *key);		// 计算哈希值
	void *(*keyDup)(void *privdata, const void *key);	// 复制键
	void *(*valDup)(void *privdata, const void *obj);	// 复制值
	int (*keyCompare)(void *privdata, const void *key1, const void *key2);	// 比较键
	void (*keyDestructor)(void *privdata, void *key);	// 摧毁键
	void (*valDestructor)(void *privdata, void *obj);	// 摧毁值
} dictType;

// 哈希表
typedef struct _dictht 
{
	dictEntry **table;		// 哈希表数组
	unsigned long size;		// 哈希表大小
	unsigned long sizemask;	// 哈希表大小掩码，用于计算索引值
	unsigned long used;		// 已有节点的数量
} dictht;

// 字典
typedef struct _dict 
{
	dictType *type;	// 字典类型，保存一些用于操作特定类型键值对的函数
	void *privdata;	// 私有数据，保存需要传给那些类型特定函数的可选数据
	dictht ht[2];	// 一个字典包括两个哈希表
	long rehashidx;	// rehash索引，不进行rehash时值为-1
	int iterators;	// 当前正在使用的迭代器数量
} dict;

// 字典迭代器
/* 如果safe值为1，意味着在迭代过程中可以安全调用
* dictAdd, dictFind及其他一些操作方法,
* 否则只有调用dictFind()方法是安全的 */
typedef struct _dictIterator 
{
	dict *d;	// 当前使用的字典
	long index;	// 当前迭代器下标
	int table, safe; //table指示字典中散列表下标，safe指明该迭代器是否安全
	dictEntry *entry, nextEntry; // 键值对节点指针
	long long fingerprint; /* unsafe iterator fingerprint for misuse detection. */
} dictIterator;

// 遍历回调函数
typedef void (dictScanFunction)(void *privdata, const dictEntry *de);

// 每个hash table的初始长度
#define DICT_HT_INITIAL_SIZE	4

// 释放值
#define dictFreeVal(d, entry) \
	if ((d)->type->valDestructor) \
		((d)->type->valDestructor((d)->privdata, (entry)->v.val))

// 更新节点的值
#define dictSetVal(d, entry, _val_) do { \
	if ((d)->type->valDup) \
		entry->v.val = (d)->type->valDup((d)->privdata, _val_); \
	else \
		entry->v.val = (_val_); \
} while (0)

// 释放键
#define dictFreeKey(d, entry) \
	if ((d)->type->keyDestructor) \
		((d)->type->keyDestructor((d)->privdata, (entry)->key))

// 更新节点的键
#define dictSetKey(d, entry, _key_) do { \
	if ((d)->type->keyDup) \
		entry->key = (d)->type->keyDup((d)->privdata, _key_); \
	else \
		entry->key = (_key_); \
} while (0)

// 比较键
#define dictCompareKeys(d, key1, key2) \
	(((d)->type->keyCompare) ? \
		(d)->type->keyCompare((d)->privdata, key1, key2) : (key1) == (key2))

#define dictHashKey(d, key) (d)->type->hashFunction(key) //计算哈希值
#define dictGetKey(he) ((he)->key)
#define dictGetVal(he) ((he)->v.val)
#define dictSize(d) ((d)->ht[0].used + (d)->ht[1].used)
#define dictIsRehashing(d) ((d)->rehashidx != -1)

dict *dictCreate(dictType *type, void *privDataPtr);
int dictResize(dict *d);
int dictExpand(dict *d, unsigned long size);
int dictAdd(dict *d, void *key, void *val);
dictEntry *dictAddRaw(dict *d, void *key);
int dictReplace(dict *d, void *key, void *val);
dictEntry *dictReplaceRaw(dict *d, void *key);
int dictDelete(dict *d, const void *key);
int dictDeleteNoFree(dict *d, const void *key);
void dictRelease(dict *d);
dictEntry *dictFind(dict *d, const void *key);
dictEntry *dictGetRandomKey(dict *d);
size_t dictGetSomeKeys(dict *d, dictEntry **des, size_t count);
void dictEmpty(dict *d, void(callback)(void*));
void dictEnableResize(void);
void dictDisableResize(void);
int dictRehash(dict *d, int n);
int dictRehashMilliseconds(dict *d, int ms);
void dictSetHashFunctionSeed(size_t initval);
size_t dictGetHashFunctionSeed(void);

#endif