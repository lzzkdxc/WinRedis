#include <limits.h>


#include "util.h"

/* string ת�� long long, �ɹ�����1��������value��ʧ�ܷ���0*/
int string2ll(const char *s, size_t slen, long long *value)
{
	const char *p = s;
	size_t plen = 0;
	int negative = 0; //�Ƿ�Ϊ���� 0:���� 1:����
	unsigned long long v;

	// �ַ���Ϊ��
	if (plen == slen)
		return 0;
	
	// �������:ֻ��һλ����0
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

		// ֻ��һ������
		if (plen == slen)
			return 0;
	}

	/* ���˸����⣬��λ����Ϊ1-9������ת��ʧ��*/
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
		if (v > (ULLONG_MAX / 10)) /* ��� */
			return 0;
		v *= 10;

		if (v > (ULLONG_MAX - (p[0] - '0'))) /* ��� */
			return 0;
		v += p[0] - '0';

		p++;
		plen++;
	}

	/* ����������е�λ������ʹ�ã���ʧ�� */
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
