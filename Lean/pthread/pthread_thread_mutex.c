#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>

void *thread_function(void *arg);

//sem_t bin_sem;
pthread_mutex_t work_mutex;

#define WORK_SIZE 1024
char work_area[WORK_SIZE];
int time_to_exit = 0;

int main()
{
	int res;
	pthread_t a_thread;
	void *thread_result;

	//res = sem_init(&bin_sem, 0, 0);
	res = pthread_mutex_init(&work_mutex, NULL);
	
	if(res != 0)
	{
		perror("Initialization failed");
		exit(EXIT_FAILURE);
	}
	res = pthread_create(&a_thread, NULL, thread_function, NULL);
	if(res != 0)
	{
		perror("Thread failed failed");
		exit(EXIT_FAILURE);		
	}
	pthread_mutex_lock(&work_mutex);
	printf("Input some text, enter 'end' to finish...\n");
#if 0
	while(strncmp("end", work_area, 3) != 0)
	{
		fgets(work_area, WORK_SIZE, stdin);
		sem_post(&bin_sem);
	}
#endif
	while(!time_to_exit)
	{
		fgets(work_area, WORK_SIZE, stdin);
		pthread_mutex_unlock(&work_mutex);
		while(1)
		{
			pthread_mutex_lock(&work_mutex);
			if(work_area[0] != '\0')
			{
				pthread_mutex_unlock(&work_mutex);
				sleep(1);
			}
			else
			{
				break;
			}
		}
	}
	pthread_mutex_unlock(&work_mutex);
	printf("\nWaiting for thread to finish...\n");
	res = pthread_join(a_thread, &thread_result);
	if(res != 0)
	{
		perror("Thread joined failed");
		exit(EXIT_FAILURE);
	}
	printf("Thread joined, %s\n", (char *)thread_result);
	//sem_destroy(&bin_sem);
	pthread_mutex_destroy(&work_mutex);	
	exit(EXIT_SUCCESS);
}  
void *thread_function(void *arg)
{
	sleep(1);
	pthread_mutex_lock(&work_mutex);
	while(strncmp("end", work_area, 3) != 0)
	{
		printf("You input %d characters\n", strlen(work_area)-1);
		work_area[0] = '\0';
		pthread_mutex_unlock(&work_mutex);
		sleep(1);
		pthread_mutex_lock(&work_mutex);
		if(work_area[0] == '\0')
		{
			pthread_mutex_unlock(&work_mutex);
			sleep(1);
			pthread_mutex_lock(&work_mutex);
		}
	}
	time_to_exit = 1;
	work_area[0] = '\0';
	pthread_mutex_unlock(&work_mutex);
	pthread_exit(0);
}
#if 0
void *thread_function(void *arg)
{
	sem_wait(&bin_sem);
	while(strncmp("end", work_area, 3) != 0)
	{
		printf("You input %d characters\n",strlen(work_area)-1);
		sem_wait(&bin_sem);
	}
	pthread_exit("Thread_function is finished!");
}
#endif
