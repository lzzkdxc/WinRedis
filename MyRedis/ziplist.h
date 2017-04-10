#ifndef __ZIPLIST_H
#define __ZIPLIST_H

/* ���ݽṹ
������������������������������������������������������������������������������������������
zlbytes|zltail|zllen|entry1|...|entryN|zlend|
������������������������������������������������������������������������������������������
---ziplist header---|-----entries-----|-end-|
������������������������������������������������������������������������������������������

zlbytes����ʾѹ���б�ռ���ڴ���ֽ���
zltail����ʾѹ���б�ͷ��β֮���ƫ����
zllen����ʾѹ���б��нڵ������
zlen����ʾѹ���б���������ֵ�̶�Ϊ0xFF

entry: �ڵ�ṹ
������������������������������������������������������������������������
prev_entry_length|encoding|contents|
������������������������������������������������������������������������

prev_entry_length������ǰ�ýڵ�ĳ��ȣ����ڴӺ���ǰ������
				   ���ǰ�ýڵ�ĳ���С��254�ֽڣ�����1���ֽ������泤�ȣ�
				   ��֮������5���ֽ������棬���е�һ���ֽڱ�����Ϊ0xFE(254),�����ĸ��ֽ��������洢ǰ�ýڵ�ĳ���ֵ
encoding����������
contents�����𱣴�ڵ��ֵ
*/

#define ZIPLIST_HEAD 0
#define ZIPLIST_TAIL 1

unsigned char *ziplistNew();
unsigned char *ziplistPush(unsigned char *zl, unsigned char *s, unsigned int slen, int where);
#endif