#include <limits.h>

#include "util.h"

/* string 转成 long long, 成功返回1，并存入value，失败返回0*/
int string2ll(const char *s, size_t slen, long long *value)
{
	const char *p = s;
	size_t plen = 0;
	int negative = 0; //是否为负数 0:正数 1:负数
	unsigned long long v;

	// 字符串为空
	if (plen == slen)
		return 0;
	
	// 特殊情况:只有一位且是0
	if (slen == 1 && p[0] == '0')
	{
		if (value != NULL) *value = 0;
		return 1;
	}

	if (p[0] == '-')
	{
		negative = 1;
		p++;
		plen++;

		// 只有一个负号
		if (plen == slen)
			return 0;
	}

	/* 除了负号外，首位必须为1-9，否则转换失败*/
	if (p[0] >= '1' && p[0] <= '9')
	{
		v = p[0] - '0';
		p++;
		plen++;
	}
	else if (p[0] == '0' && slen == 1)
	{
		*value = 0;
		return 1;
	}
	else
	{
		return 0;
	}

	while (plen < slen && p[0] >= '0' && p[0] <= '9')
	{
		if (v > (ULLONG_MAX / 10)) /* 溢出 */
			return 0;
		v *= 10;

		if (v > (ULLONG_MAX - (p[0] - '0'))) /* 溢出 */
			return 0;
		v += p[0] - '0';

		p++;
		plen++;
	}

	/* 如果不是所有的位数都被使用，则失败 */
	if (plen < slen)
		return 0;

	if (negative == 1)
	{
		if (v > ((unsigned long long)(-(LLONG_MIN + 1)) + 1))
			return 0;
		if (value != NULL) *value = v;
	}
	else
	{
		if (v > LLONG_MAX)
			return 0;
		if (value != NULL) *value = v;
	}
	return 1;
}
