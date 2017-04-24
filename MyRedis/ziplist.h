#ifndef __ZIPLIST_H
#define __ZIPLIST_H

/* 数据结构
—————————————————————————————————————————————
zlbytes|zltail|zllen|entry1|...|entryN|zlend|
—————————————————————————————————————————————
---ziplist header---|-----entries-----|-end-|
—————————————————————————————————————————————

zlbytes：表示压缩列表占总内存的字节数
zltail：表示压缩列表头和尾之间的偏移量
zllen：表示压缩列表中节点的数量
zlend：表示压缩列表结束，其值固定为0xFF

entry: 节点结构
————————————————————————————————————
prev_entry_length|encoding|contents|
————————————————————————————————————

prev_entry_length：编码前置节点的长度，用于从后往前遍历，
				   如果前置节点的长度小于254字节，采用1个字节来保存长度，
				   反之，则用5个字节来保存，其中第一个字节被设置为0xFE(254),后面四个字节则用来存储前置节点的长度值
encoding：编码属性
contents：负责保存节点的值
*/

#define ZIPLIST_HEAD 0
#define ZIPLIST_TAIL 1

unsigned char *ziplistNew();
unsigned char *ziplistMerge(unsigned char **first, unsigned char **second);
unsigned char *ziplistPush(unsigned char *zl, unsigned char *s, unsigned int slen, int where);
unsigned char *ziplistIndex(unsigned char *zl, int index);
unsigned char *ziplistNext(unsigned char *zl, unsigned char *p);
unsigned char *ziplistPrev(unsigned char *zl, unsigned char *p);
unsigned int ziplistGet(unsigned char *p, unsigned char **sval, unsigned int *slen, long long *lval);
unsigned char *ziplistInsert(unsigned char *zl, unsigned char *p, unsigned char *s, unsigned int slen);
unsigned char *ziplistDelete(unsigned char *zl, unsigned char **p);
unsigned char *ziplistDeleteRange(unsigned char *zl, int index, unsigned int num);
unsigned int ziplistCompare(unsigned char *p, unsigned char *s, unsigned int slen);
unsigned char *ziplistFind(unsigned char *p, unsigned char *vstr, unsigned int vlen, unsigned int skip);
unsigned int ziplistLen(unsigned char *zl);
unsigned int ziplistBlobLen(unsigned char *zl);

#endif