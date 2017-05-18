#include <limits.h>

#include "util.h"

/* 计算数字V转换成字符串所需的位数 */
uint32_t digits10(uint64_t v)
{
	if (v < 10) return 1;
	if (v < 100) return 2;
	if (v < 1000) return 3;
	if (v < 1000000000000UL) 
	{
		if (v < 100000000UL) 
		{
			if (v < 1000000) 
			{
				if (v < 10000) return 4;
				return 5 + (v >= 100000);
			}
			return 7 + (v >= 10000000UL);
		}
		if (v < 10000000000UL) 
		{
			return 9 + (v >= 1000000000UL);
		}
		return 11 + (v >= 100000000000UL);
	}
	return 12 + digits10(v / 1000000000000UL);
}

/* long long转成string */
int ll2string(char *dst, size_t dstlen, long long svalue)
{
	static const char digits[201] =
		"0001020304050607080910111213141516171819"
		"2021222324252627282930313233343536373839"
		"4041424344454647484950515253545556575859"
		"6061626364656667686970717273747576777879"
		"8081828384858687888990919293949596979899";
	int negative;
	unsigned long long value;

	// 判断是否是负数，并记录
	if (svalue < 0)
	{
		if (svalue != LLONG_MIN)
			value = -svalue;
		else
			value = ((unsigned long long)LLONG_MAX) + 1;
		negative = 1;
	}
	else
	{
		value = svalue;
		negative = 0;
	}

	// 检查长度
	uint32_t const length = digits10(value) + negative;
	if (length >= dstlen) return 0;

	uint32_t next = length;
	dst[next] = '\0';
	next--;
	while (value >= 100)
	{
		int const i = (value % 100) * 2;
		value /= 100;
		dst[next] = digits[i + 1];
		dst[next - 1] = digits[i];
		next -= 2;
	}

	// 处理最后1-2位
	if (value < 10)
	{
		dst[next] = '0' + (uint32_t)value;
	}
	else
	{
		int i = (uint32_t)value * 2;
		dst[next] = digits[i + 1];
		dst[next - 1] = digits[i];
	}

	if (negative == 1) dst[0] = '-';
	return length;
}

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
