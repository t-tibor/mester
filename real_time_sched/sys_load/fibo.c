#include <stdio.h>


int main()
{
	long v1, v2;
	v1=0;
	v2=1;

	while(1)
	{
		printf("%d;\n",v2);
		v1 = v1+v2;
		printf("%d;\n",v1);
		v2 = v1+v2;
	}
}