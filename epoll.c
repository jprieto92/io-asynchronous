/*
 *	SAUL ALONSO MONSALVE, NIA: 100303517
 *	JAVIER PRIETO CEPEDA, NIA: 100307011
 */

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <poll.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/epoll.h>

/************************************ MACROS *********************************/

//#define DEBUG

#ifdef DEBUG
#define DEBUG_PRINT printf
#else
#define DEBUG_PRINT(...)
#endif

#define READ_THREAD 0
#define WRITE_THREAD 1

#define MAX_MASTER 1000		// Number of reads/writes at master before print results
#define MAX_ERR_STR   128    	// Number of characters in error strings
#define READ_TIMEOUT  100   	// Sellect timeout, in milliseconds
#define WRITE_TIMEOUT  100  	// Sellect timeout, in milliseconds

/* 
 * Closes and frees the resources used in the program. 
 * Call on error or exit. 
 */
#define FREE_ALL \
	if (threads != NULL) { \
		free(threads); \
		threads = NULL; \
	} \
	if (pipes != NULL) { \
		int i; \
		for (i=0; i<nPipes; i++) { \
			close(pipes[2*i]); \
			close(pipes[2*i + 1]); \
		} \
		free(pipes); \
		pipes = NULL; \
	}


/******************************* TYPE DEFINITIONS ****************************/

/*
 * Stores the necessary information that will be passed as argument
 * to each thread.
 */
typedef struct {
	pthread_t thread_id;
	int index;
	int n_pipes;
	int* pipes;
} thread_params;



/***************************** FUNCTION DECLARATIONS ************************/

/*
 * Captures SIGINT to set the termination flag to true.
 */
void handler(int signal);

/*
 * Performs the poll operations on the write sides of all the pipes,
 * on every thread.
 *
 * @param pipes  - Array of pipe file descriptors (both read and write)
 * @param nPipes - Number of pipes contained in the previous array
 *
 * @return Zero on susccessful operation, -1 otherwise
 */
int master(int* pipes, int nPipes, int nPipesPerThread, int nReaderThreads, int nWriterThreads);

/*
 * Performs the poll operations on the read sides of the pipes assigned
 * to a particular thread.
 *
 * This function is passed as argument to the pthread_create calls in the
 * main function.
 *
 * @param args - Struct of type thread_params that holds all the parameters
 *               needed for the thread
 */
void* slaveWriter(void *args);
void* slaveReader(void *args);

// Sleep a random time of microseconds
void mySleep(int threadType){
	unsigned long microseconds;

	// Case read: random value smaller than READ_TIMEOUT
	if(threadType == READ_THREAD)
		microseconds = (unsigned long)((rand() % READ_TIMEOUT)*1000);
	// Case write: random value smaller than WRITE_TIMEOUT
	else if(threadType == WRITE_THREAD)
		microseconds = (unsigned long)((rand() % WRITE_TIMEOUT)*1000);
	
	usleep(microseconds);
}

/******************************* GLOBAL vARIABLES *****************************/

/* Flag that indicates the threads to break the execution of the polling loop */
int terminate = 0;

/******************************************************************************/


/*
 *	main
 */
int main(int argc, char *argv[])
{
	int nPipesPerThread;
	int nThreads,nWriterThreads,nReaderThreads;
	int nPipes;

	int* pipes = NULL;
	thread_params* threads = NULL;

	struct sigaction act;

	int i,j;
	char err[MAX_ERR_STR+1];

        /* Read parameters */
	if (argc < 4) {
		DEBUG_PRINT("\nInvalid input.\n\tUsage:\t./pipes <number of write threads> <number of read threads> <number of pipes per thread>\n\n");
		exit(-1);
	}

	srand(time(NULL));

	nWriterThreads 		= strtol(argv[1], NULL, 10);
	nReaderThreads 		= strtol(argv[2], NULL, 10);
	nThreads=nWriterThreads+nReaderThreads;
	nPipesPerThread = strtol(argv[3], NULL, 10);
	nPipes 			= nPipesPerThread * nThreads;
	
	/* Initialise control vectors */
	if ((threads = (thread_params*) malloc(nThreads * sizeof(thread_params))) == NULL) {
		fprintf(stderr, "Memory error.\n");
		FREE_ALL
		exit(-1);
	}

	if ((pipes = (int*) malloc(2*nPipes * sizeof(int))) == NULL) {
		fprintf(stderr, "Memory error.\n");
		FREE_ALL
		exit(-1);
	}


	/* Create M pipes */
	for (i=0; i<nPipes; i++) {
		if (pipe(&pipes[2*i]) == -1) {
			sprintf(err, "Pipe error (%d)", i);
			perror(err);
			FREE_ALL
			exit(-1);
		}
	}

	/* Set SIGINT to terminate the program in a clean way */
	act.sa_handler = handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;

	if (sigaction(SIGINT, &act, NULL) == -1) {
		perror("Signal error");
		FREE_ALL
		exit(-1);
	}

	/* Create N threads, joinable by default, and build parameters for each */
	for (j=0; j<nWriterThreads; j++) { // First half of the threads are writter
		threads[j].index        = j;
		threads[j].n_pipes      = nPipesPerThread;
		threads[j].pipes        = &pipes[2*j * nPipesPerThread];
		
		/* BORRAR */
		int k;
		for(k=0; k<nPipesPerThread*2; k+=2)
			DEBUG_PRINT("R: %d, W: %d\n", threads[j].pipes[k], threads[j].pipes[k+1]);
		/* FIN BORRAR */

		if (pthread_create(&threads[j].thread_id, NULL, slaveWriter, (void*) &threads[j]) != 0) {
			sprintf(err, "Thread error (%d)", j);
			perror(err);
			FREE_ALL
			exit(-1);
		}
	}
	for (j=nWriterThreads; j<nWriterThreads+nReaderThreads; j++) { // First half of the threads are writter
		threads[j].index        = j;
		threads[j].n_pipes      = nPipesPerThread;
		threads[j].pipes        = &pipes[2*j * nPipesPerThread];
			
		/* BORRAR */
		int k;
		for(k=0; k<nPipesPerThread*2; k+=2)
			DEBUG_PRINT("R: %d, W: %d\n", threads[j].pipes[k], threads[j].pipes[k+1]);
		/* FIN BORRAR */

		if (pthread_create(&threads[j].thread_id, NULL, slaveReader, (void*) &threads[j]) != 0) {
			sprintf(err, "Thread error (%d)", j);
			perror(err);
			FREE_ALL
			exit(-1);
		}
	}	
	/* Monitor pipes */
	if (master(pipes, nPipes, nPipesPerThread, nWriterThreads, nReaderThreads) != 0) {
		fprintf(stderr, "Polling error.\n");
		FREE_ALL
		exit(-1);
	}

	/* Wait for threads to finish */
	for (i=0; i<nThreads; i++) {
		pthread_join(threads[i].thread_id, NULL);
	}

	/* Clean up */
        fprintf(stdout, "\n\tMaster thread exiting...\n\n");
	FREE_ALL

	exit(0);
}


/*
 *	slaveWriter
 */
void* slaveWriter(void *args)
{
    /* Recover parameters */
    thread_params* params = (thread_params *) args;
    int i, epfd, ready;
    struct epoll_event ev;
    struct epoll_event evlist[params->n_pipes];

    DEBUG_PRINT("Starting slaveWriter%d\n", params->index);

    // Create epoll
    if((epfd = epoll_create1(0)) < 0){
	printf("slaveWriter(): epoll_create error\n");	
	exit(0);
    }

    // Fill structure with the necessary fds
    for(i=0; i<params->n_pipes; i++){
	int fd = params->pipes[i*2+1];
	ev.events = EPOLLOUT;
	ev.data.fd = fd;
	if(epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev)){
		printf("slaveWriter(): epoll_ctl error\n");
		exit(0);
	}
    }

    // Main polling loop
    while (!terminate) {
	// Epoll wait
	ready = epoll_wait(epfd, evlist, params->n_pipes, -1);
	if(ready == -1){
		printf("slaveWriter(): epoll_wait error\n");
		exit(0);
	}

	// Find available fd
	for(i = rand() % ready; ; i = (i+1) % ready){
		if(evlist[i].events & EPOLLOUT){
			int element = rand();
			// Write element in pipe
			DEBUG_PRINT("slaveWriter%d: Writing %d in descriptor %d\n", params->index, element, evlist[i].data.fd);	
			if(write(evlist[i].data.fd, &element, sizeof(int)) != sizeof(int)){
				fprintf(stderr, "slaveWriter(): Writing in pipe error.\n");
                		exit(-1);
			}
			// Sleep random number of milliseconds
   			mySleep(WRITE_THREAD);
			break;
		}
	}
    }

    fprintf(stdout, "\tThread %d exiting...\n", params->index);
    return NULL;
}

/*
 *	slaveReader
 */
void* slaveReader(void *args)
{
    /* Recover parameters */
    thread_params* params = (thread_params *) args;
    int i, epfd, ready;
    struct epoll_event ev;
    struct epoll_event evlist[params->n_pipes];

    DEBUG_PRINT("Starting slaveReader%d\n", params->index);

    // Create epoll
    if((epfd = epoll_create1(0)) < 0){
	printf("slaveReader(): epoll_create error\n");	
	exit(0);
    }

    // Fill structure with the necessary fds
    for(i=0; i<params->n_pipes; i++){
	int fd = params->pipes[i*2];
	ev.events = EPOLLIN;
	ev.data.fd = fd;
	if(epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev)){
		printf("slaveReader(): epoll_ctl error\n");
		exit(0);
	}
    }

    // Main polling loop
    while (!terminate) {
	// Epoll wait
	ready = epoll_wait(epfd, evlist, params->n_pipes, -1);
	if(ready == -1){
		printf("slaveReader(): epoll_wait error\n");
	}

	// Find available fd
	for(i = rand() % ready; ; i = (i+1) % ready){
		if(evlist[i].events & EPOLLIN){
			int element;
			// Read element from the pipe
			if(read(evlist[i].data.fd, &element, sizeof(int)) != sizeof(int)){
				fprintf(stderr, "slaveReader(): Writing in pipe error.\n");
                		exit(-1);
			}
			DEBUG_PRINT("slaveReader%d: Reading %d in descriptor %d\n", params->index, element, evlist[i].data.fd);
			// Sleep random number of milliseconds
   			mySleep(WRITE_THREAD);
			break;
		}
	}
    }

    fprintf(stdout, "\tThread %d exiting...\n", params->index);
    return NULL;
}

/*
 * mater
 */
int master(int* pipes, int nPipes, int nPipesPerThread, int nReaderThreads, int nWriterThreads)
{
    int i, j=0, k=0, count_i, count_j, count_k, epfd_read, epfd_write, ready_read, ready_write, fd_read, fd_write;
    struct epoll_event ev;
    struct epoll_event evlist_read[nWriterThreads*nPipesPerThread];
    struct epoll_event evlist_write[nReaderThreads*nPipesPerThread];
    
    // Slaves pipes
    int *writePipes = pipes;
    int *readPipes = pipes + (nWriterThreads*nPipesPerThread*2); 

    DEBUG_PRINT("Starting master\n");

    // Create epoll read
    if((epfd_read = epoll_create1(0)) < 0){
	printf("master(): epoll_create error\n");	
	exit(0);
    }

    // Create epoll write
    if((epfd_write = epoll_create1(0)) < 0){
	printf("master(): error in epoll_create\n");	
	exit(0);
    }

    // Fill structure with the necessary fds
    for(i=0; i<nWriterThreads*nPipesPerThread; i++){
	fd_read = *(writePipes+i*2);	
	ev.events = EPOLLIN;
	ev.data.fd = fd_read;
	if(epoll_ctl(epfd_read, EPOLL_CTL_ADD, fd_read, &ev)){
		printf("master(): epoll_ctl error\n");
		exit(0);
	}
    }
 
    // Fill structure with the necessary fds
    for(i=0; i<nWriterThreads*nPipesPerThread; i++){
	fd_write = *(readPipes+i*2+1);
	ev.events = EPOLLOUT;
	ev.data.fd = fd_write;
	if(epoll_ctl(epfd_write, EPOLL_CTL_ADD, fd_write, &ev)){
		printf("master(): error in epoll_ctl\n");
		exit(0);
	}
    }
   
    /* Main polling loop */
    while (!terminate) {
	// Epoll wait
	ready_read = epoll_wait(epfd_read, evlist_read, nWriterThreads*nPipesPerThread, -1);
	if(ready_read == -1){
		printf("master(): epoll_wait error\n");
		exit(0);
	}

	// Find available fd
	for(i = rand() % ready_read, count_i = 0; count_i < ready_read; i = (i+1) % ready_read, count_i++){
		if(evlist_read[i].events & EPOLLIN){
			int element, thread;
			// Read element from the pipe
			if(read(evlist_read[i].data.fd, &element, sizeof(int)) != sizeof(int)){
				fprintf(stderr, "master(): Writing in pipe error.\n");
                		exit(-1);
			}

			DEBUG_PRINT("master: Reading %d from descriptor %d\n", element, evlist_read[i].data.fd);
			
			// Thread = (Element) MOD (number of readers)
			thread = element%(nReaderThreads);

			DEBUG_PRINT("Selected read thread number %d\n", thread);

			// Iterate until element is written in pipe
			while(1){
				int written = 0;
				ready_write = epoll_wait(epfd_write, evlist_write, nReaderThreads*nPipesPerThread, -1);
				if(ready_write == -1){
					printf("master(): error in epoll_wait\n");
					exit(0);
				}

				// Iterate over thread pipes write file descriptors
				for(count_k = 0; count_k<nPipesPerThread && !written; count_k++, k=(k+1)%nPipesPerThread){
					// Iterate over evlist_write
					for(count_j = 0; count_j < ready_write; j = (j+1) % ready_write, count_j++){
						// Check if thre pile write file descriptor is in evlist_write
						if((evlist_write[j].events & EPOLLOUT) && (evlist_write[j].data.fd == readPipes[(thread*nPipesPerThread+k)*2+1])){
							// Write element in pipe
							if(write(evlist_write[j].data.fd, &element, sizeof(int)) != sizeof(int)){
								fprintf(stderr, "master(): Writing in pipe error.\n");
                						exit(-1);
							}
							written = 1;
							DEBUG_PRINT("master: Writing %d in desciptor %d\n", element, evlist_write[j].data.fd);
							break;
						}
					}
				}
				if(written) break;
			}	
		}
	}
    }
    return 0;
}

void handler(int signal)
{
       fprintf(stdout, "\n\tSIGTERM received, terminating...\n\n");
}

