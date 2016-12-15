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
	
	if(pipe(file_pipes) == 0)
	{
		fork_result = fork();
		if(fork_result == -1)
		{
			fprintf(stderr, "Fork failure");
			exit(EXIT_FAILURE);
		}
		if(fork_result == 0)
		{
			//data_processed = read(file_pipes[0], buffer, BUFSIZ);
			sprintf(buffer, "%d", file_pipes[0]);
			printf("s = %s\n", buffer);			
			(void)execl("process_pipe_recv", "process_pipe_recv", buffer, (char *)0);
			exit(EXIT_SUCCESS);
		}
		else
		{
			data_processed = write(file_pipes[1], some_data, strlen(some_data));
			printf("%d - Wrote %d bytes\n",getpid(),  data_processed);
			//exit(EXIT_SUCCESS);
		}
	}
	exit(EXIT_SUCCESS);
}
