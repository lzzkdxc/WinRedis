#include "adlist.h"
#include "zmalloc.h"
#include <assert.h>

// ����һ���յ�����
list *listCreate(void)
{
	list *newlist;
	if ((newlist = (list*)zmalloc(sizeof(list))) == nullptr) // �����ڴ�
		return nullptr;

	// ��ʼ����������
	newlist->head = newlist->tail = nullptr;
	newlist->len = 0;
	newlist->dup = nullptr;
	newlist->free = nullptr;
	newlist->match = nullptr;
	return newlist;
}

// �ͷ���������
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
		// ����Զ������ͷź����������
		if (l->free) l->free(current->value);
		zfree(current);
		current = next;
	}
	zfree(l);
}

// ��ͷ������һ���ڵ�
list *listAddNodeHead(list *l, void *value)
{
	listNode *node;
	if ((node = (listNode*)zmalloc(sizeof(listNode))) == nullptr)
		return nullptr;

	node->value = value;
	if (l->len == 0)  // ������
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

// ��β������һ���ڵ�
list *listAddNodeTail(list *l, void *value)
{
	listNode *node;
	if ((node = (listNode*)zmalloc(sizeof(listNode))) == nullptr)
		return nullptr;

	node->value = value;
	if (l->len == 0)  // ������
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

// ����λ�ò���ڵ�
// ���У�old_nodeΪ����λ��
//      valueΪ����ڵ��ֵ
//      afterΪ0ʱ��ʾ����old_nodeǰ�棬Ϊ1ʱ��ʾ����old_node����
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

// ɾ���ڵ�
void listDelNode(list *l, listNode *node)
{
	assert(node != nullptr);
	
	if (node->prev != nullptr)
		node->prev->next = node->next;
	else // ɾ���ڵ�Ϊͷ�ڵ���Ҫ�ı�head��ָ��
		l->head = node->next;
	if (node->next != nullptr)
		node->next->prev = node->prev;
	else // ɾ���ڵ�Ϊβ�ڵ���Ҫ�ı�tail��ָ��
		l->tail = node->prev;

	if (l->free != nullptr) l->free(node->value);
	zfree(node);
	l->len--;
}

// ��ȡ������
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

// �ͷŵ�����
void listReleaseIterator(listIter *iter)
{
	zfree(iter);
}

// ����Ϊ���������
void listRewind(list *l, listIter *iter)
{
	iter->next = l->head;
	iter->direction = AL_START_HEAD;
}

// ����Ϊ���������
void listRewindTail(list *l, listIter *iter)
{
	iter->next = l->tail;
	iter->direction = AL_START_TAIL;
}

// ��ȡ��һ��������
listNode *listNext(listIter *iter)
{
	listNode *current = iter->next;

	if (current != nullptr)
		iter->next = iter->direction == AL_START_HEAD ? current->next : current->prev;

	return current;
}

// ������������
list *listDup(list *orig)
{
	list *copylist;
	listIter iter;
	listNode *node;

	if ((copylist = listCreate()) == nullptr)
		return nullptr;
	// ���ƽڵ�ֵ��������
	copylist->dup = orig->dup;
	copylist->free = orig->free;
	copylist->match = orig->match;

	listRewind(orig, &iter);
	while ((node = listNext(&iter)) != nullptr)
	{
		void *value;
		// ���ƽڵ�
		// ���������dup����������dup���������ƽڵ�ֵ
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
		// ������β����ӽڵ�
		if (listAddNodeTail(copylist, value) == nullptr)
		{
			listRelease(copylist);
			return nullptr;
		}
	}
	return copylist;
}

// ���ݽڵ�ֵ���ҽڵ�
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

// ������������ҽڵ�
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

// ������ת
// ����β�ڵ��Ƴ���Ȼ����뵽��ͷ����Ϊ�µı�ͷ
void listRotate(list *l)
{
	listNode *tail = l->tail;

	if (listLength(l) <= 1) return;

	// ȡ��β�ڵ�
	l->tail = tail->prev;
	l->tail->next = nullptr;
	// �����ƶ�����ͷ����Ϊ�µı�ͷָ��
	l->head->prev = tail;
	tail->prev = nullptr;
	tail->next = l->head;
	l->head->prev = tail;
}