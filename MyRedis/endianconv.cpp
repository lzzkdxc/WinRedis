﻿#include "endianconv.h"

void memrev16(void *p)
{
	unsigned char *x = (unsigned char *)p;
	unsigned char t;

	t = x[0];
	x[0] = x[1];
	x[1] = t;
}

void memrev32(void *p)
{
	unsigned char *x = (unsigned char *)p;
	unsigned char t;

	t = x[0];
	x[0] = x[3];
	x[3] = t;
	t = x[1];
	x[1] = x[2];
	x[2] = t;
}

void memrev64(void *p) {
	unsigned char *x = (unsigned char *)p;
	unsigned char t;

	t = x[0];
	x[0] = x[7];
	x[7] = t;
	t = x[1];
	x[1] = x[6];
	x[6] = t;
	t = x[2];
	x[2] = x[5];
	x[5] = t;
	t = x[3];
	x[3] = x[4];
	x[4] = t;
}

uint16_t intrev16(uint16_t v)
{
	memrev16(&v);
	return v;
}

uint32_t intrev32(uint32_t v)
{
	memrev32(&v);
	return v;
}

uint64_t intrev64(uint64_t v)
{
	memrev64(&v);
	return v;
}

#ifdef REDIS_TEST
#include <stdio.h>

#define UNUSED(x) (void)(x)
int endianconvTest(int argc, char *argv[]) {
	char buf[32];

	UNUSED(argc);
	UNUSED(argv);

	sprintf(buf, "ciaoroma");
	memrev16(buf);
	printf("%s\n", buf);

	sprintf(buf, "ciaoroma");
	memrev32(buf);
	printf("%s\n", buf);

	sprintf(buf, "ciaoroma");
	memrev64(buf);
	printf("%s\n", buf);

	return 0;
}
#endif
