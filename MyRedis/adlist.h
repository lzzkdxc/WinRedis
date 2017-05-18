#ifndef __ADLIST_H
#define __ADLIST_H


// 链表节点
typedef struct _listNode
{
	struct _listNode *prev;	// 前一个节点
	struct _listNode *next;	// 后一个节点
	void *value;			// 节点值
} listNode;

// 迭代器
typedef struct _listIter
{
	listNode *next;			// 指向下一个节点
	int direction;			// 方向参数，正序和逆序
} listIter;

// 链表
typedef struct _list
{
	listNode *head;			// 指向头节点
	listNode *tail;			// 指向尾节点
	void *(*dup)(void *ptr);	// 自定义节点复制函数
	void (*free)(void *ptr);	// 自定义节点释放函数
	int (*match)(void *ptr, void *key);	// 自定义节点匹配函数
	unsigned long len;		// 链表长度
} list;


#define listLength(l) ((l)->len)  // 获取list长度
#define listFirst(l) ((l)->head)  // 获取list头节点指针
#define listLast(l) ((l)->tail)   // 获取list尾节点指针

#define listPrevNode(n) ((n)->prev)		// 获取当前节点的前一个节点
#define listNextNode(n) ((n)->next)		// 获取当前节点的后一个节点
#define listNodeValue(n) ((n)->value)	// 获取当前节点的值

#define listSetDupMethod(l,m) ((l)->dup = (m))		// 设置节点复制函数
#define listSetFreeMethod(l,m) ((l)->free = (m))    // 设置节点释放函数
#define listSetMatchMethod(l,m) ((l)->match = (m))  // 设置节点匹配函数

#define listGetDupMethod(l) ((l)->dup)		// 获取节点复制函数
#define listGetFreeMethod(l) ((l)->free)		// 获取节点释放函数
#define listGetMatchMethod(l) ((l)->match)	// 获取节点匹配函数

// 迭代器方向的宏定义
#define AL_START_HEAD 0		// 从头到尾
#define AL_START_TAIL 1		// 从尾到头

list *listCreate(void);
void listRelease(list *l);
list *listAddNodeHead(list *l, void *value);
list *listAddNodeTail(list *l, void *value);
list *listInsertNode(list *l, listNode *old_node, void *value, int after);
void listDelNode(list *l, listNode *node);
listIter *listGetIterator(list *l, int direction);
listNode *listNext(listIter *iter);
void listReleaseIterator(listIter *iter);
list *listDup(list *orig);
listNode *listSearchKey(list *l, void *key);
listNode *listIndex(list *l, long index);
void listRewind(list *l, listIter *iter);
void listRewindTail(list *l, listIter *li);
void listRotate(list *l);

#endif