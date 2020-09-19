#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#if defined (READLINE_LIBRARY)
#  include "readline.h"
#  include "history.h"
#else
#  include <readline/readline.h>
#  include <readline/history.h>
#endif

int
main (int c, char **v)
{
	char *input;

	for (;;) {
		input = readline ((char *)NULL);
		if (input == 0)
			break;
		printf ("%s\n", input);
		if (strcmp (input, "exit") == 0)
			break;
		free (input);
	}
	exit (0);
}
