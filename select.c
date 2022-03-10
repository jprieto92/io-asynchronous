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
#include <sys/select.h>

/************************************ MACROS *********************************/

//#define DEBUG

#ifdef DEBUG
#define DEBUG_PRINT printf
#else
#define DEBUG_PRINT(...)
#endif

#define READ_THREAD 0
#define WRITE_THREAD 1

#define MAX_MASTER 1000	     // Number of reads/writes at master before print results
#define MAX_ERR_STR   128    // Number of characters in error strings
#define READ_TIMEOUT  100    // Sellect timeout, in milliseconds
#define WRITE_TIMEOUT  100   // Sellect timeout, in milliseconds

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
		printf("\nInvalid input.\n\tUsage:\t./pipes <number of write threads> <number of read threads> <number of pipes per thread>\n\n");
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
		
		int k;
		for(k=0; k<nPipesPerThread*2; k+=2)
			DEBUG_PRINT("R: %d, W: %d\n", threads[j].pipes[k], threads[j].pipes[k+1]);

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
			
		int k;
		for(k=0; k<nPipesPerThread*2; k+=2)
			DEBUG_PRINT("R: %d, W: %d\n", threads[j].pipes[k], threads[j].pipes[k+1]);

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
    fd_set fds;
    int index = 0, i, nfds;

    DEBUG_PRINT("Starting slaveWriter%d\n", params->index);

    // Main polling loop
    while (!terminate) {
	FD_ZERO(&fds);		// Free fds structure
	nfds = 0;		// nfds

	// Fill structure with the necessary fds
	for(i=0; i<params->n_pipes; i++){
		int fd = params->pipes[i*2+1];
		if(fd >= nfds) nfds = fd + 1;
		FD_SET(fd, &fds);   
	}

	// Select
	if(select(nfds, NULL, &fds, NULL, NULL) == -1){
		printf("Error in select\n");
		exit(0);
	}
	
	// Find available fd and write and element in it
	while(1){
		int fd = params->pipes[index*2+1];
		index = (index+1) % params->n_pipes;
		if(FD_ISSET(fd, &fds)){
			int element = rand();
			// Write element in pipe
			DEBUG_PRINT("slaveWriter%d: Writing %d in descriptor %d\n", params->index, element, fd);	
			if(write(fd, &element, sizeof(int)) != sizeof(int)){
				fprintf(stderr, "Error in slaveWriter(): Writing in pipe error.\n");
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
    fd_set fds;
    int index = 0, i, nfds;

    DEBUG_PRINT("Starting slaveReader%d\n", params->index);

    // Main polling loop
    while (!terminate) {
	FD_ZERO(&fds);		// Free fds structure
	nfds = 0;		// nfds

	// Fill structure with the necessary fds
	for(i=0; i<params->n_pipes; i++){
		int fd = params->pipes[i*2];
		if(fd >= nfds) nfds = fd + 1;
		FD_SET(fd, &fds);   
	}

	// Select
	if(select(nfds, &fds, NULL, NULL, NULL) == -1){
		printf("Error in select\n");
		exit(0);
	}
	
	// Find available fd and read and element from it
	while(1){
		int fd = params->pipes[index*2];
		index = (index+1) % params->n_pipes;	
		if(FD_ISSET(fd, &fds)){
			int element;
			// Read element from pipe
			if(read(fd, &element, sizeof(int)) != sizeof(int)){
				fprintf(stderr, "Error in slaveReader(): Reading in pipe error.\n");
                		exit(-1);
			}
			DEBUG_PRINT("slaveReader%d: Reading %d in descriptor %d\n", params->index, element, fd);

			// Sleep random number of milliseconds
   			mySleep(READ_THREAD);
			break;
		}
	}
    }

    fprintf(stdout, "\tThread %d exiting...\n", params->index);
    return NULL;
}

int master(int* pipes, int nPipes, int nPipesPerThread, int nReaderThreads, int nWriterThreads)
{
    int i, j, fd_read, fd_write, aux = 0, nfds_read = 0, nfds_write = 0;
    fd_set readfds, writefds;

    // Slaves reader pipes
    int *writePipes = pipes;
    int *readPipes = pipes + (nWriterThreads*nPipesPerThread*2); 

    DEBUG_PRINT("Starting master\n");

    /* Main polling loop */
    while (!terminate) {
	// Free structures
	FD_ZERO(&readfds);

	// Fill structure
	for(i=0; i < nWriterThreads*nPipesPerThread; i++){
		fd_read = *(writePipes+i*2);
		if(fd_read >= nfds_read) nfds_read = fd_read+1;
		FD_SET(fd_read, &readfds);   
	}

	// Select
	if(select(nfds_read, &readfds, NULL, NULL, NULL) == -1){
		printf("Error in select\n");
		exit(0);
	}

	// Iterate over descriptors
	for(i=0; i<nWriterThreads*nPipesPerThread; i++){
		fd_read = *(writePipes+i*2);
		// Check if descriptor available
		 if(FD_ISSET(fd_read, &readfds)){
			int element, thread;
			FD_ZERO(&writefds);
			
			// Read from pipe
			if(read(fd_read, &element, sizeof(int)) != sizeof(int)){
				fprintf(stderr, "Error in master(): Reading in pipe error.\n");
                		exit(-1);
			}	

			DEBUG_PRINT("master: Reading %d from descriptor %d\n", element, fd_read);
			
			// Thread = (Element) MOD (number of readers)
			thread = element%(nReaderThreads);

			// Fill structure
			for(j=0; j<nPipesPerThread; j++){
				fd_write = readPipes[(thread*nPipesPerThread+j)*2+1];
				if(fd_write >= nfds_write) nfds_write = fd_write+1;
				FD_SET(fd_write, &writefds);
			}

			// Select
			if(select(nfds_write, NULL, &writefds, NULL, NULL) == -1){
				printf("Error in select\n");
				exit(0);
			}

			// Write element
			while(1){
				// Select random pipe
				aux = (aux+1) % nPipesPerThread;
				fd_write = readPipes[(thread*nPipesPerThread+aux)*2+1];	
				if(FD_ISSET(fd_write, &writefds)){
					if(write(fd_write, &element, sizeof(int)) != sizeof(int)){
						fprintf(stderr, "Error in master(): Writing in pipe error.\n");
                				exit(-1);
					}
					break;
				}
			}

			DEBUG_PRINT("master: Writing %d in desciptor %d\n", element, fd_write);
		}
	}
    }
    return 0;
}

void handler(int signal)
{
        fprintf(stdout, "\n\tSIGTERM received, terminating...\n\n");
}

