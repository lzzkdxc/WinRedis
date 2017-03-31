#ifndef __ADLIST_H
#define __ADLIST_H


// ����ڵ�
typedef struct _listNode
{
	struct _listNode *prev;	// ǰһ���ڵ�
	struct _listNode *next;	// ��һ���ڵ�
	void *value;			// �ڵ�ֵ
} listNode;

// ������
typedef struct _listIter
{
	listNode *next;			// ָ����һ���ڵ�
	int direction;			// ������������������
} listIter;

// ����
typedef struct _list
{
	listNode *head;			// ָ��ͷ�ڵ�
	listNode *tail;			// ָ��β�ڵ�
	void *(*dup)(void *ptr);	// �Զ���ڵ㸴�ƺ���
	void (*free)(void *ptr);	// �Զ���ڵ��ͷź���
	int (*match)(void *ptr, void *key);	// �Զ���ڵ�ƥ�亯��
	unsigned long len;		// ������
} list;


#define listLength(l) ((l)->len)  // ��ȡlist����
#define listFirst(l) ((l)->head)  // ��ȡlistͷ�ڵ�ָ��
#define listLast(l) ((l)->tail)   // ��ȡlistβ�ڵ�ָ��

#define listPrevNode(n) ((n)->prev)		// ��ȡ��ǰ�ڵ��ǰһ���ڵ�
#define listNextNode(n) ((n)->next)		// ��ȡ��ǰ�ڵ�ĺ�һ���ڵ�
#define listNodeValue(n) ((n)->value)	// ��ȡ��ǰ�ڵ��ֵ

#define listSetDupMethod(l,m) ((l)->dup = (m))		// ���ýڵ㸴�ƺ���
#define listSetFreeMethod(l,m) ((l)->free = (m))    // ���ýڵ��ͷź���
#define listSetMatchMethod(l,m) ((l)->match = (m))  // ���ýڵ�ƥ�亯��

#define listGetDupMethod(l) ((l)->dup)		// ��ȡ�ڵ㸴�ƺ���
#define listGetFreeMethod(l) ((l)->free)		// ��ȡ�ڵ��ͷź���
#define listGetMatchMethod(l) ((l)->match)	// ��ȡ�ڵ�ƥ�亯��

// ����������ĺ궨��
#define AL_START_HEAD 0		// ��ͷ��β
#define AL_START_TAIL 1		// ��β��ͷ

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