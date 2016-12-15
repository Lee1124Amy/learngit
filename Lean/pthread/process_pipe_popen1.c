#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

int main()
{
	FILE *write_fp;
	char buffer[BUFSIZ + 1];

	//memset(buffer, '\0', sizeof(buffer));
	sprintf(buffer, "Once upon a time , there was...\n");
	write_fp = popen("od -c", "w");
	//read_fp = popen("uname -a", "r");
#if 0
	if(read_fp != NULL)
	{
		chars_read = fread(buffer, sizeof(char), BUFSIZ, read_fp);
		if(chars_read > 0)
		{
			printf("optput was :-\n%s\n", buffer);
		}
		pclose(read_fp);
		exit(EXIT_SUCCESS);
	}
#endif
	if(write_fp != NULL)
	{
		fwrite(buffer, sizeof(char), strlen(buffer), write_fp);
		pclose(write_fp);
		exit(EXIT_SUCCESS);
	}
	exit(EXIT_FAILURE);
}