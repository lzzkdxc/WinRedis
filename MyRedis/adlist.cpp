#include "adlist.h"
#include "zmalloc.h"
#include <assert.h>

// 创建一个空的链表
list *listCreate(void)
{
	list *newlist;
	if ((newlist = (list*)zmalloc(sizeof(list))) == nullptr) // 申请内存
		return nullptr;

	// 初始化各个参数
	newlist->head = newlist->tail = nullptr;
	newlist->len = 0;
	newlist->dup = nullptr;
	newlist->free = nullptr;
	newlist->match = nullptr;
	return newlist;
}

// 释放整个链表
void listRelease(list *l)
{
	if (l == nullptr)
		return;

	unsigned long len = l->len;
	listNode *current = l->head;
	listNode *next;

	while (len--)
	{
		next = current->next;
		// 如果自定义了释放函数，则调用
		if (l->free) l->free(current->value);
		zfree(current);
		current = next;
	}
	zfree(l);
}

// 向头部插入一个节点
list *listAddNodeHead(list *l, void *value)
{
	listNode *node;
	if ((node = (listNode*)zmalloc(sizeof(listNode))) == nullptr)
		return nullptr;

	node->value = value;
	if (l->len == 0)  // 空链表
	{
		l->head = l->tail = node;
		node->prev = node->next = nullptr;
	}
	else
	{
		node->prev = nullptr;
		node->next = l->head;
		l->head->prev = node;
		l->head = node;
	}
	l->len++;
	return l;
}

// 向尾部插入一个节点
list *listAddNodeTail(list *l, void *value)
{
	listNode *node;
	if ((node = (listNode*)zmalloc(sizeof(listNode))) == nullptr)
		return nullptr;

	node->value = value;
	if (l->len == 0)  // 空链表
	{
		l->head = l->tail = node;
		node->prev = node->next = nullptr;
	}
	else
	{
		node->next = nullptr;
		node->prev = l->tail;
		l->tail->next = node;
		l->tail = node;
	}
	l->len++;
	return l;
}

// 任意位置插入节点
// 其中，old_node为插入位置
//      value为插入节点的值
//      after为0时表示插在old_node前面，为1时表示插在old_node后面
list *listInsertNode(list *l, listNode *old_node, void *value, int after)
{
	listNode *node;
	if ((node = (listNode*)zmalloc(sizeof(listNode))) == nullptr)
		return nullptr;

	node->value = value;
	if (after == 0)
	{
		node->prev = old_node->prev;
		node->next = old_node;
		if (l->head == old_node)
			l->head = node;
	}
	else
	{
		node->prev = old_node;
		node->next = old_node->next;
		if (l->tail == old_node)
			l->tail = node;
	}

	if (node->prev != nullptr)
		node->prev->next = node;
	if (node->next != nullptr)
		node->next->prev = node;
	l->len++;
	return l;
}

// 删除节点
void listDelNode(list *l, listNode *node)
{
	assert(node != nullptr);
	
	if (node->prev != nullptr)
		node->prev->next = node->next;
	else // 删除节点为头节点需要改变head的指向
		l->head = node->next;
	if (node->next != nullptr)
		node->next->prev = node->prev;
	else // 删除节点为尾节点需要改变tail的指向
		l->tail = node->prev;

	if (l->free != nullptr) l->free(node->value);
	zfree(node);
	l->len--;
}

// 获取迭代器
listIter *listGetIterator(list *l, int direction)
{
	listIter *iter;
	if ((iter = (listIter*)zmalloc(sizeof(listIter))) != nullptr)
		return nullptr;
	if (direction == AL_START_HEAD)
		iter->next = l->head;
	else
		iter->next = l->tail;
	iter->direction = direction;

	return iter;
}

// 释放迭代器
void listReleaseIterator(listIter *iter)
{
	zfree(iter);
}

// 重置为正向迭代器
void listRewind(list *l, listIter *iter)
{
	iter->next = l->head;
	iter->direction = AL_START_HEAD;
}

// 重置为逆向迭代器
void listRewindTail(list *l, listIter *iter)
{
	iter->next = l->tail;
	iter->direction = AL_START_TAIL;
}

// 获取下一个迭代器
listNode *listNext(listIter *iter)
{
	listNode *current = iter->next;

	if (current != nullptr)
		iter->next = iter->direction == AL_START_HEAD ? current->next : current->prev;

	return current;
}

// 复制整个链表
list *listDup(list *orig)
{
	list *copylist;
	listIter iter;
	listNode *node;

	if ((copylist = listCreate()) == nullptr)
		return nullptr;
	// 复制节点值操作函数
	copylist->dup = orig->dup;
	copylist->free = orig->free;
	copylist->match = orig->match;

	listRewind(orig, &iter);
	while ((node = listNext(&iter)) != nullptr)
	{
		void *value;
		// 复制节点
		// 如果定义了dup函数，则按照dup函数来复制节点值
		if (copylist->dup != nullptr)
		{
			value = copylist->dup(node->value);
			if (value == nullptr)
			{
				listRelease(copylist);
				return nullptr;
			}
		}
		else
		{
			value = node->value;
		}
		// 依次向尾部添加节点
		if (listAddNodeTail(copylist, value) == nullptr)
		{
			listRelease(copylist);
			return nullptr;
		}
	}
	return copylist;
}

// 根据节点值查找节点
listNode *listSearchKey(list *l, void *key)
{
	listIter iter;
	listNode *node;

	listRewind(l, &iter);
	while ((node = listNext(&iter)) != nullptr)
	{
		void *value;
		if ((l->match != nullptr && l->match(node->value, key)) ||
			(node->value == key))
		{
			return node;
		}
	}
	return nullptr;
}

// 根据序号来查找节点
listNode *listIndex(list *l, long index)
{
	listNode *node;

	if (index < 0)
	{
		node = l->tail;
		index = -index - 1;
		while (index-- && node != nullptr) node = node->prev;
	}
	else
	{
		node = l->head;
		while (index-- && node != nullptr) node = node->next;
	}
	return node;
}

// 链表旋转
// 将表尾节点移除，然后插入到表头，成为新的表头
void listRotate(list *l)
{
	listNode *tail = l->tail;

	if (listLength(l) <= 1) return;

	// 取出尾节点
	l->tail = tail->prev;
	l->tail->next = nullptr;
	// 将其移动到表头并成为新的表头指针
	l->head->prev = tail;
	tail->prev = nullptr;
	tail->next = l->head;
	l->head->prev = tail;
}