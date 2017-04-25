#include "server.h"
#include <math.h>

/* 创建跳跃表节点 */
zskiplistNode *zslCreateNode(int level, double score, robj *obj)
{
	zskiplistNode *zn = (zskiplistNode*)zmalloc(sizeof(zskiplistNode) + level * sizeof(zskiplistNode::zskiplistLevel));
	zn->score = score;
	zn->obj = obj;
	return zn;
}

/* 创建跳跃表 */
zskiplist *zslCreate()
{
	int j;
	zskiplist *zsl;

	zsl = (zskiplist*)zmalloc(sizeof(*zsl));
	zsl->level = 1;
	zsl->length = 0;
	// 创建一个层数为32，分值为0，成员对象为NULL的表头结点
	zsl->header = zslCreateNode(ZSKIPLIST_MAXLEVEL, 0, NULL);
	for (j = 0; j < ZSKIPLIST_MAXLEVEL; j++)
	{
		zsl->header->level[j].forward = NULL;
		zsl->header->level[j].span = 0;
	}
	zsl->header->backward = NULL;
	zsl->tail = NULL;
	return zsl;
}

void zslFreeNode(zskiplistNode *node)
{
	decrRefCount(node->obj);
	zfree(node);
}

void zslFree(zskiplist *zsl)
{
	zskiplistNode *node = zsl->header->level[0].forward, *next;

	zfree(zsl->header);
	while (node)
	{
		next = node->level[0].forward;
		zslFreeNode(node);
		node = next;
	}
	zfree(zsl);
}

zskiplistNode *zslInsert(zskiplist *zsl, double score, robj *obj)
{
	zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x;
	unsigned int rank[ZSKIPLIST_MAXLEVEL];
	int i, level;

	serverAssert(!isnan(score));
	x = zsl->header;
	for (i = zsl->level - 1; i >= 0; i--)
	{
		rank[i] = i == (zsl->level - 1) ? 0 : rank[i + 1];
		while (x->level[i].forward && 
			(x->level[i].forward->score < score ||
				(x->level[i].forward->score == score &&
				compareStringObjects(x->level[i].forward->obj, obj) < 0)))
		{
			rank[i] += x->level[i].span;
			x = x->level[i].forward;
		}
		update[i] = x;
	}

	return x;
}