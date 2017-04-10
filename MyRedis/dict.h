#ifndef __DICT_H
#define __DICT_H

#include <stdint.h>

#define DICT_OK 0
#define DICT_ERR 1

// ��ϣ��ڵ�
typedef struct _dictEntry 
{
	void *key;	// ��
	union {
		void *val;
		uint64_t u64;
		int64_t s64;
		double d;
	} v;	// ֵ
	struct _dictEntry *next; // ָ����һ����ϣ�ڵ㣬ʹ�������������ϣ��ͻ
} dictEntry;

// �ֵ�����
typedef struct _dictType 
{
	unsigned int (*hashFunction)(const void *key);		// �����ϣֵ
	void *(*keyDup)(void *privdata, const void *key);	// ���Ƽ�
	void *(*valDup)(void *privdata, const void *obj);	// ����ֵ
	int (*keyCompare)(void *privdata, const void *key1, const void *key2);	// �Ƚϼ�
	void (*keyDestructor)(void *privdata, void *key);	// �ݻټ�
	void (*valDestructor)(void *privdata, void *obj);	// �ݻ�ֵ
} dictType;

// ��ϣ��
typedef struct _dictht 
{
	dictEntry **table;		// ��ϣ������
	unsigned long size;		// ��ϣ���С
	unsigned long sizemask;	// ��ϣ���С���룬���ڼ�������ֵ
	unsigned long used;		// ���нڵ������
} dictht;

// �ֵ�
typedef struct _dict 
{
	dictType *type;	// �ֵ����ͣ�����һЩ���ڲ����ض����ͼ�ֵ�Եĺ���
	void *privdata;	// ˽�����ݣ�������Ҫ������Щ�����ض������Ŀ�ѡ����
	dictht ht[2];	// һ���ֵ����������ϣ��
	long rehashidx;	// rehash������������rehashʱֵΪ-1
	int iterators;	// ��ǰ����ʹ�õĵ���������
} dict;

// �ֵ������
/* ���safeֵΪ1����ζ���ڵ��������п��԰�ȫ����
* dictAdd, dictFind������һЩ��������,
* ����ֻ�е���dictFind()�����ǰ�ȫ�� */
typedef struct _dictIterator 
{
	dict *d;	// ��ǰʹ�õ��ֵ�
	long index;	// ��ǰ�������±�
	int table, safe; //tableָʾ�ֵ���ɢ�б��±꣬safeָ���õ������Ƿ�ȫ
	dictEntry *entry, nextEntry; // ��ֵ�Խڵ�ָ��
	long long fingerprint; /* unsafe iterator fingerprint for misuse detection. */
} dictIterator;

// �����ص�����
typedef void (dictScanFunction)(void *privdata, const dictEntry *de);

// ÿ��hash table�ĳ�ʼ����
#define DICT_HT_INITIAL_SIZE	4

// �ͷ�ֵ
#define dictFreeVal(d, entry) \
	if ((d)->type->valDestructor) \
		((d)->type->valDestructor((d)->privdata, (entry)->v.val))

// ���½ڵ��ֵ
#define dictSetVal(d, entry, _val_) do { \
	if ((d)->type->valDup) \
		entry->v.val = (d)->type->valDup((d)->privdata, _val_); \
	else \
		entry->v.val = (_val_); \
} while (0)

// �ͷż�
#define dictFreeKey(d, entry) \
	if ((d)->type->keyDestructor) \
		((d)->type->keyDestructor((d)->privdata, (entry)->key))

// ���½ڵ�ļ�
#define dictSetKey(d, entry, _key_) do { \
	if ((d)->type->keyDup) \
		entry->key = (d)->type->keyDup((d)->privdata, _key_); \
	else \
		entry->key = (_key_); \
} while (0)

// �Ƚϼ�
#define dictCompareKeys(d, key1, key2) \
	(((d)->type->keyCompare) ? \
		(d)->type->keyCompare((d)->privdata, key1, key2) : (key1) == (key2))

#define dictHashKey(d, key) (d)->type->hashFunction(key) //�����ϣֵ
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