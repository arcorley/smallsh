#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

int fg_only = 0;

void sigtstp_handler(int signo)
{
	int c;
	if (fg_only == 0) //if we aren't in foreground mode
	{
		fg_only = 1; //set the flag 
		char* message = "\nEntering foreground-only mode (& is now ignored) \n"; //display the message
		write(STDOUT_FILENO, message, 51); //write it out to the screen
		fflush(stdout);
	}
	else //do the opposite if we're already in foreground-mode
	{
		fg_only = 0;
		char* message = "\nExiting foreground-only mode \n";
		write(STDOUT_FILENO, message, 31);
		fflush(stdout);

	}
}

//this function replaces a substring in a larger string
void substrReplace(char string[], char search[], char replace[])
{
	char buffer[100];
	char* p = string;
	while ((p = strstr(p, search)))
	{
		strncpy(buffer, string, p - string);
		buffer[p - string] = '\0';
		strcat(buffer, replace);
		strcat(buffer, p + strlen(search));
		strcpy(string, buffer);
		p++;
	}
}

int main(){
	char* line = NULL;
	char* inputFile = NULL;
	char* outputFile = NULL;
	char* args[513];
	char* token;
	int myfile;
	int background;
	int status = 0;
	int pid;
	int exited = 0;
	int i;
	int numArgs;
	char expandLine[100], search[3] = "$$", process[100];
	int c;
	int charsEntered;

	//handler for SIGINT signal. don't kill the shell if it's received
	struct sigaction sigint_action = {0}, sigtstp_action = {0};
	sigint_action.sa_handler = SIG_IGN; //ignore sigint signals in the parent process
	sigtstp_action.sa_handler = &sigtstp_handler;
	sigfillset(&sigtstp_action.sa_mask);
	//sigtstp_action.sa_flags = SA_RESTART;
	sigaction(SIGINT, &sigint_action, NULL);
	sigaction(SIGTSTP, &sigtstp_action, NULL);

	//loop for handling commands
	while (!exited){
		background = 0; //initialize background variable to 0

		printf(": "); //prompt for the shell
		fflush(stdout); //flush after printing to the screen

		//read in the user's input
		ssize_t size = 0;
		
		charsEntered = getline(&line, &size, stdin);
		if (charsEntered == -1)
			clearerr(stdin);


		//this section parses the input line into arguments
		numArgs = 0;

		token = strtok(line, " \n"); 

		while (token != NULL)
		{
			//this section handles input redirection
			if (strcmp(token, "<") == 0)
			{
				token = strtok(NULL, " \n"); //get the input file name
				inputFile = strdup(token); //copy the token into the file name variable

				token = strtok(NULL, " \n"); //move token for next loop
			}
			//this section handles output redirection
			else if (strcmp(token, ">") == 0)
			{
				token = strtok(NULL, " \n"); //get the output file name
				outputFile = strdup(token); //copy the token into the file name variable

				token = strtok(NULL, " \n");
			}
			//this section handles running processes in the background
			else if (strcmp(token, "&") == 0)
			{
				if (fg_only == 0) //if not in foreground only mode, set the background flag to 1
				{
					background = 1; //set background flag to 1
					break; //exit the loop because this command can only come at the end
				}

				break;
			}
			//this section handles any other command or argument
			else
			{
				//run a check to see if $$ appears in the argument
				if (strstr(token, "$$") != NULL)
				{
					int shellPid = getpid();
					sprintf(process, "%d", shellPid); //convert the pid to a string

					strcpy(expandLine, token); //copy the line with the $$ into a char array
					substrReplace(expandLine, search, process); //replace the substring with the pid
					token = expandLine; //copy the expanded line back over to the token;
				}

				args[numArgs] = strdup(token);

				token = strtok(NULL, " \n");
				numArgs++;
			}
		}

		args[numArgs] = NULL; //make last element in arg array be null

		//check for a blank line or a comment. skip if this is the case
		if (args[0] == NULL || !(strncmp(args[0], "#", 1)))
		{
			//do nothing
		}
		//check if exit command is received
		else if (strcmp(args[0], "exit") == 0)
		{
			exit(0); //exit loop and set flag to 1
			exited = 1; 
		}
		//check if status command is received
		else if (strcmp(args[0], "status") == 0)
		{
			if (WIFEXITED(status)) //check if exit value was received
			{
				printf("exit value %d\n", WEXITSTATUS(status));
				fflush(stdout);
			}
			else //if no exit value, then signal was received
			{
				printf("terminated by signal %d\n", status);
				fflush(stdout);
			}
		}
		//check if cd command was received
		else if (strcmp(args[0], "cd") == 0)
		{
			if (args[1] == NULL) //if only one arg was received, go to home directory
			{
				chdir(getenv("HOME"));
			}
			else //otherwise, go to directory specified in args
			{
				chdir(args[1]);
			}
		}
		//otherwise, execute command by forking a child process
		else
		{
			pid = fork(); //fork off a child proces

			//this section handles the instructions for the child process
			if (pid == 0)
			{
				//if this is a foreground process
				if (!background)
				{
					sigint_action.sa_handler = SIG_DFL; //set the handler to default so child processes can be killed
					sigaction(SIGINT, &sigint_action, NULL);
				}
				//check for input redirection
				if (inputFile != NULL)
				{
					myfile = open(inputFile, O_RDONLY); //open file in read only mode

					//if there is an error opening the file, print an error message
					if (myfile == -1)
					{
						fprintf(stderr, "cannot open %s for input\n", inputFile);
						fflush(stdout);
						exit(1); //set status code to 1
					}
					else if (dup2(myfile, 0) == -1) //redirect the input, return error if it is not successful
					{
						fprintf(stderr, "error redirecting input\n");
						fflush(stdout);
						exit(1);
					}
					close(myfile);
				}

				//check for background process requests
				if (background)
				{
					//check if input file was already specified. If so, do nothing. Otherwise, redirect to /dev/null
					if (inputFile == NULL)
					{
						myfile = open("/dev/null", O_RDONLY);

						//return error message if there was an issue opening /dev/null
						if (myfile == -1)
						{
							fprintf(stderr, "cannot open /dev/null for input\n");
							fflush(stdout);
							exit(1);
						}
						else if (dup2(myfile, 0) == -1) //redirect the input, return error if it is not successful
						{
							fprintf(stderr, "error redirecting input\n");
							fflush(stdout);
							exit(1);
						}
						close(myfile);
					}
				}

				//check for output redirection
				if (outputFile != NULL)
				{
					myfile = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);

					//return error message if there was an issue opening output file
					if (myfile == -1)
					{
						fprintf(stderr, "cannot open %s for output\n", outputFile);
						fflush(stdout);
						exit(1);
					}
					else if (dup2(myfile, 1) == -1) //redirect the output, return error if unsuccessful
					{
						fprintf(stderr, "error redirecting output\n");
						fflush(stdout);
						exit(1);
					}
					close(myfile);
				}

				//execute the command. if there is an error, return error message
				if (execvp(args[0], args))
				{
					fprintf(stderr, "%s: no such file or directory\n", args[0]);
					fflush(stdout);
					exit(1);
				}
			}
			//this section handles a fork error
			else if (pid < 0)
			{
				fprintf(stderr, "error forking a child process\n");
				fflush(stdout);
				status = 1;
				break;
			}
			//instructions for the parent
			else
			{
				if (!background)
				{
					do{
						waitpid(pid, &status, 0);
					} while(!WIFEXITED(status) && !WIFSIGNALED(status));

					if (WIFSIGNALED(status))
					{
						printf("terminated by signal %d\n", status);
						fflush(stdout);
					}
				}
				else
				{
					printf("background pid is %d\n", pid);
					fflush(stdout);
				}
			}
		}

		//empty the arg array for next line
		for (i = 0; i < numArgs; i++)
		{
			args[i] = NULL;
		}

		//blank out file names
		inputFile = NULL;
		outputFile = NULL;

		//check if background process has finished
		pid = waitpid(-1, &status, WNOHANG);
		while (pid > 0)
		{
			if (WIFEXITED(status))
			{
				printf("background pid %d is done: exit value %d\n", pid, status);
				fflush(stdout);
			}
			else
			{
				printf("background pid %d is done: terminated by signal %d\n", pid, status);
				fflush(stdout);
			}

			pid = waitpid(-1, &status, WNOHANG);
		}
	}

	return 0;
}
