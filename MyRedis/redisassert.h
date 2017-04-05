/*�����滻assert.h����Redis��־�д�ӡ��ջ����*/

#ifndef __REDISASSERT_H
#define __REDISASSERT_H


#if defined(_WIN32)
#else
#include <unistd.h>
#endif
#define assert(_e)((_e)?(void)0:(_serverAssert(#_e, __FILE__, __LINE__),_exit(1)))

void _serverAssert(char *estr, char *file, int line);
#endif