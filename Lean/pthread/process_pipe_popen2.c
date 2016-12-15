#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#define BUFFSIZE 1024

int main()
{
	FILE *read_fp;
	char buffer[BUFFSIZE + 1];
	int chars_read;

	memset(buffer, '\0', sizeof(buffer));
	read_fp = popen("ps ax", "r");

	if(read_fp != NULL)
	{
		chars_read = fread(buffer, sizeof(char), BUFFSIZE, read_fp);
		while(chars_read > 0)
		{
			buffer[chars_read - 1] = '\0';
			printf("Reading %d:-\n %s\n", BUFFSIZE, buffer);
			chars_read = fread(buffer, sizeof(char), BUFFSIZE, read_fp);
		}
		pclose(read_fp);
		exit(EXIT_SUCCESS);
	}

	exit(EXIT_FAILURE);
}
