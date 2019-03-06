#include <stdio.h>
#include <semaphore.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>

// STRUCTS
struct queue{
	int buf[5];
};

// TYPEDEFS
typedef sem_t Semaphore;
typedef pthread_t Thread;
typedef int Consumable;
typedef pthread_mutex_t Mutex;
typedef struct queue Queue;

// GLOBALS
Semaphore emptySlots, fullSlots;
Mutex mutex = PTHREAD_MUTEX_INITIALIZER;
Thread *consumers;
Thread *producers;
int numProducers, numConsumers, sleepyTime;
Consumable buf[5];
bool running;

// FUNCTION DECLATARIONS
void *producerHandler(void*);
void *consumerHandler(void*);
void placeConsumable(Consumable);
Consumable produce();
Consumable getConsumable();
bool semSetup();
void destroySems();
bool threadSetup(char *argv[]);
void joinThreads();
void cleanup(int);

int main(int argc, char *argv[])
{
	//	Command line usage: pc <num producers> <num consumers> <duration>
	if(argc == 4)
	{
		if((sleepyTime = atoi(argv[3])) < 1 || (numProducers = atoi(argv[1])) < 1 || (numConsumers = atoi(argv[2])) < 1)
		{
			fprintf(stderr, "Error: Invalid arguments given to %s\nUsage:\n\t$ %s <# producer threads> <# consumer threads> <total program duration>\n", argv[0], argv[0]);
			exit(1);
		}
		if(!semSetup())
		{
			fprintf(stderr, "Error: Unable to properly initialize semaphores. Stopping...\n");
			exit(1);
		}
		if(signal(SIGINT, cleanup) == SIG_ERR)
		{
			fprintf(stderr, "Error: Unable to assign handler to SIGINT. Stopping...\n");
			destroySems();
		}
		running = true;
		if(!threadSetup(argv))
		{
			fprintf(stderr, "Error: Unable to properly setup threads. Stopping...\n");
			destroySems();
			exit(1);
		}
		sleep(sleepyTime);
		raise(SIGINT);
	}
	else
	{
		fprintf(stderr, "Error: Invalid arguments given to %s\nUsage:\n\t$ %s <# producer threads> <# consumer threads> <total program duration>\n", argv[0], argv[0]);
		exit(1);
	}
}

// FUNCTION DEFINITIONS

bool semSetup()
{
	if(sem_init(&emptySlots, 0, 5) < 0 || sem_init(&fullSlots, 0, 0))
		return false;
	else
		return true;
}

bool threadSetup(char *argv[])
{
	numProducers = atoi(argv[1]);
	numConsumers = atoi(argv[2]);

	producers = (Thread *)malloc(numProducers * sizeof(Thread));
	consumers = (Thread *)malloc(numConsumers * sizeof(Thread));

	if(producers == NULL || consumers == NULL)
		return false;

	for(int i = 0; i < numProducers; i++)
	{
		// TODO Do I need to modify this to use Thread** instead? Does this work?
		Thread current;
		pthread_create(&current, NULL, producerHandler, NULL);
		producers[i] = current;
	}
	for(int i = 0; i < numConsumers; i++)
	{
		Thread current;
		pthread_create(&current, NULL, consumerHandler, NULL);
		consumers[i] = current;
	}
	return true;
}

void cleanup(int signum)
{
	running = false;

	printf("Received SIGINT, performing cleanup...\n");

	// terminate all the running threads
	printf("Terminating threads...\n");
	joinThreads();

	// destroy all of the semaphores
	printf("Destroying semaphores...\n");
	destroySems();

	// terminate
	printf("Done.\n");
	exit(0);
}

void joinThreads()
{
	for(int i = 0; i < numConsumers; i++)
	{
		if(consumers[i] != 0)
			pthread_join(consumers[i], NULL);
	}
	for(int i = 0; i < numProducers; i++)
	{
		if(producers[i] != 0)
			pthread_join(producers[i], NULL);
	}
	free(producers);
	free(consumers);
}

void destroySems()
{
	sem_destroy(&emptySlots);
	sem_destroy(&fullSlots);
}

void *producerHandler(void *args)
{
	while(running)
	{
		Consumable production = produce();
		sem_wait(&emptySlots);
		pthread_mutex_lock(&mutex);
		printf("Produced %d.\n", production);
		placeConsumable(production);
		pthread_mutex_unlock(&mutex);
		sem_post(&fullSlots);
	}

	printf("Producer terminating...\n");

	return NULL;
}

void *consumerHandler(void *args)
{
	while(running)
	{
		sem_wait(&fullSlots);
		pthread_mutex_lock(&mutex);
		Consumable c = getConsumable();
		printf("Consumed %d.\n", c);
		pthread_mutex_unlock(&mutex);
		sem_post(&emptySlots);
	}

	printf("Consumer terminating...\n");
	return NULL;
}

Consumable produce()
{
	return (Consumable)rand();
}

void placeConsumable(Consumable c)
{
	//buf assumed to have at least 1 open spot, since this code will only run
	//if a producer got a lock on emptySlots
	int index;
	sem_getvalue(&fullSlots, &index);
	buf[index] = c;
}

Consumable getConsumable()
{
	//buf assumed to have at least 1 full slot, since this code will only run
	//if a consumer got a lock on fullSlots
	int index;
	sem_getvalue(&fullSlots, &index);
	return buf[index-1];
}