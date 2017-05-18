#include <iostream>
#include "zmalloc.h"

void main(int argc, char **argv)
{
	auto m = zmalloc(10);

	system("pause");
}