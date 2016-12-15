#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

int main()
{
	int data_processed;
	int file_pipes[2];
	const char some_data[] = "1234567890";
	char buffer[BUFSIZ + 1];
	pid_t fork_result;
	

	memset(buffer, '\0', sizeof(buffer));
	printf("Helloworld1\n");	
	if(pipe(file_pipes) == 0)
	{
		printf("hellowrld3\n");
		fork_result = fork();
		printf("Hello world2\n");
		if(fork_result == -1)
		{
			fprintf(stderr, "Fork failure");
			exit(EXIT_FAILURE);
		}
		if(fork_result == 0)
		{
			data_processed = read(file_pipes[0], buffer, BUFSIZ);
			printf("Read %d bytes: %s\n", data_processed, buffer);
			exit(EXIT_SUCCESS);
		}
		else
		{
			data_processed = write(file_pipes[1], some_data, strlen(some_data));
			printf("Wrote %d bytes\n", data_processed);
			exit(EXIT_SUCCESS);
		}
	}
}
