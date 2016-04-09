#include <pthread.h>
#include <stdio.h>
#include <malloc.h>

const int thread_num = 5 ;
const int test_count = 1 ;

void test_normal(void)
{
	void *p = malloc(4) ;
	usleep(10000) ;
	free(p) ;
}

void test1(void)
{
	void *p = malloc(1) ;
}

void test2(void)
{
	void *p = malloc(3) ;
	sleep(5) ;
//	p = malloc(5) ;
}

void test3(void)
{
	void *p = malloc(4) ;
	sleep(10) ;
	free(p) ;
	sleep(20) ;
//	p = malloc(4) ;
}

void *thread_test_func(void *p)
{
	int i ;
	int index = (int)(p) ;
	printf("thread index %d\n", index) ;
	for (i = 0 ; i < test_count ; i++)
	{
		printf("thread index %d: normal\n", index) ;
		test_normal() ;
		printf("thread index %d: test1\n", index) ;
		test1() ;
		printf("thread index %d: test2\n", index) ;
		test2() ;
		printf("thread index %d: test3\n", index) ;
		test3() ;
		printf("thread index %d: end\n", index) ;
	}
}
int main(void)
{
	printf("main thread is %lu\n", pthread_self() ) ;

	int i ;
	pthread_t tid[thread_num] ;
	for (i = 0 ; i < thread_num ; i++)
	{
		pthread_create(&tid[i], NULL, thread_test_func, (void *)i) ;
		printf("new thread %lu\n", tid[i]) ;
	}

	for (i = 0 ; i < thread_num ; i++)
	{
		printf("join thread %lu\n", tid[i]) ;
		pthread_join(tid[i], NULL) ;
	}

	return 0 ;
}
