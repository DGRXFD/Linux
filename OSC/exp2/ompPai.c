#include <stdio.h>
#include <omp.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <sys/time.h>
int main()
{
	int range = 2.0;
	int inCircle = 0, total = 1e8;

	double x, y, dis;
	srand((unsigned int)time(NULL));

	struct timeval t0,t1;
	gettimeofday(&t0, NULL);

//	#pragma omp parallel for shared(inCircle)
	for(int i = 0; i < total; i++)
	{
		x = ((double)rand()/(double)(RAND_MAX)*range) - 1;
		y = ((double)rand()/(double)(RAND_MAX)*range) - 1;
		dis = sqrt(x*x+y*y);
	//	printf("%f %f %f\n",x, y, dis); 
		if(dis <= 1)
		{
			inCircle++;
		}
	}
	gettimeofday(&t1, NULL);

	double time = (t1.tv_sec - t0.tv_sec);
	printf("PAI:%f time:%f\n",4*inCircle/(1.0*total), time);
}
