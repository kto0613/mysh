#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <signal.h>
#include <dirent.h>

#include <string.h>
#include <ctype.h>
#include <fnmatch.h>

////////////////////////////////////////
//GLOBAL DEFINITIONS
////////////////////////////////////////

#define MAX_COMLEN 1024
#define MAX_ARGLEN 256
#define MAX_DIRS 16
#define MAX_HISTORIES 32
#define MAX_PROMPTLEN 64

//DEFINITIONS FOR escSequence

#define ES_NO_SEQ 0
#define ES_ARROW_UP 1
#define ES_ARROW_DOWN 2
#define ES_ARROW_RIGHT 3
#define ES_ARROW_LEFT 4
#define ES_FUNC_DELETE 5
#define ES_FUNC_HOME 6
#define ES_FUNC_END 7

////////////////////////////////////////
//GLOBAL FUNCTIONS
////////////////////////////////////////

int mysh_exit(int argc, char* argv[]);
int mysh_cd(int argc, char* argv[]);
int mysh_pushd(int argc, char* argv[]);
int mysh_dirs(int argc, char* argv[]);
int mysh_popd(int argc, char* argv[]);
int mysh_history(int argc, char* argv[]);
int mysh_prompt(int argc, char* argv[]);
int mysh_alias(int argc, char* argv[]);
int mysh_unalias(int argc, char* argv[]);
int mysh_lock(int argc, char* argv[]);
int mysh_ver(int argc, char* argv[]);

void initSignal(void);
void resetSignal(void);

int initTerm(void);
int resetTerm(void);

int escSequence(void);

int checkExcl(char* command);
int checkAlias(char* command);

int commandToArgs(char* command, char* command_args[]);
int expandArgs(char* command_args[], char* expanded_args[]);

int checkInternal(char* name);

int internalCommands(int index, int argc, char* command_args[]);
int externalCommands(int argc, char* command_args[]);

void initHistoryQueue(void);
void queueHistoryQueue(char* command);
int checkHistoryQueue(int num, char* string, int len);
void saveHistoryQueue(void);

void freeAliasList(void);

void exitShell(int exitcode);
int haveChar(char* string, char ch);
int redrawCommand(char* command, int len, int cursor, int s);

////////////////////////////////////////
//GLOBAL TYPE/STRUCT DEFINITIONS
////////////////////////////////////////

typedef int(*comfunc)(int argc, char* command_args[]);

struct COMMAND {
	char* name;
	comfunc func;
};

struct HISTORY {
	char* command;
	int num;
};

struct ALIAS {
	char* alias;
	char* command;
	struct ALIAS* next;
};

////////////////////////////////////////
//GLOBAL VARIABLES
////////////////////////////////////////

struct termios old, cur;

int myshOntty;
int foreground;

const char* mysh_version = "mysh v0.4";

const struct COMMAND commands[] = {
	{ "exit", mysh_exit },
	{ "cd", mysh_cd },
	{ "pushd", mysh_pushd },
	{ "dirs", mysh_dirs },
	{ "popd", mysh_popd },
	{ "history", mysh_history },
	{ "prompt", mysh_prompt },
	{ "alias", mysh_alias },
	{ "unalias", mysh_unalias },
	{ "lock", mysh_lock },
	{ "ver", mysh_ver }
};
const int nCommands = sizeof(commands) / sizeof(struct COMMAND);

char* dirStack[MAX_DIRS];
int pDirStack = 0;

struct HISTORY historyQueue[MAX_HISTORIES];
int startHistoryQueue = 0;
int nHistoryQueue = 0;

char prompt[MAX_PROMPTLEN] = "mysh$";

struct ALIAS* aliasList = 0;

////////////////////////////////////////
//FUNCTION main
////////////////////////////////////////

int main(int argc, char *argv[]) {
	char command[MAX_COMLEN];
	char* command_args[MAX_ARGLEN];
	char* expanded_args[MAX_ARGLEN];
	int ch, ret, nargs;
	int commandlen, len;
	int history, historyIndex;
	int i, isEnd = 0, cursor, s;

	if (!isatty(0)) myshOntty = 0;
	else myshOntty = 1;

	if (myshOntty) {
		initHistoryQueue();
		initSignal();
	}

main_start:
	if (myshOntty) {
		ret = initTerm();
		if (ret < 0) {
			perror("mysh: initTerm()");
			exitShell(1);
		}
	}

command_start:
	if (myshOntty) {
		redrawCommand(command, 0, 0, 0);

		commandlen = cursor = s = 0;
		history = 0;
		while ((ch = getchar()) != '\n') {
			if (isprint(ch)) {
				if (history != 0) {
					strcpy(command, historyQueue[historyIndex].command);
					commandlen = len;
					history = 0;
				}
				if (commandlen < MAX_COMLEN - 1) {
					for (i = commandlen; i > cursor; i--) {
						command[i] = command[i - 1];
					}
					command[cursor] = ch;
					commandlen++;
					cursor++;
					s = redrawCommand(command, commandlen, cursor, s);
				}
			} //if (isprint(ch))
			else {
				switch (ch) {
				case 8: //Backspace
				case 127: //Backspace(DEL)
					if (cursor > 0) {
						if (history != 0) {
							strcpy(command, historyQueue[historyIndex].command);
							commandlen = len;
							history = 0;
						}
						for (i = cursor; i < commandlen; i++) {
							command[i-1] = command[i];
						}
						commandlen--;
						cursor--;
						s = redrawCommand(command, commandlen, cursor, s);
					}
					break;
				case -1: //EOF
				case 4: //EOT
					putchar(10);
					goto command_end;
				case 3: //SIGINT
					fputs("^C\n", stdout);
					goto command_start;
				case 27: //Escape
					switch (escSequence()) {
					case ES_ARROW_UP:
						if ((i = checkHistoryQueue(history - 1, NULL, 0)) != -1) {
							historyIndex = i;
							history -= 1;
							len = strlen(historyQueue[historyIndex].command);
							cursor = len;
							s = redrawCommand(historyQueue[historyIndex].command, len, cursor, 0);
						}
						break;
					case ES_ARROW_DOWN:
						if (history != 0) {
							historyIndex = checkHistoryQueue(++history, NULL, 0);
							if (historyIndex == -1) history = 0;
							if (history == 0) {
								cursor = commandlen;
								s = redrawCommand(command, commandlen, cursor, s);
							}
							else {
								len = strlen(historyQueue[historyIndex].command);
								cursor = len;
								s = redrawCommand(historyQueue[historyIndex].command, len, cursor, 0);
							}
						}
						break;
					case ES_ARROW_RIGHT:
						if (history != 0 && cursor < len) {
							cursor++;
							s = redrawCommand(historyQueue[historyIndex].command, len, cursor, s);
						}
						else if (cursor < commandlen) {
							cursor++;
							s = redrawCommand(command, commandlen, cursor, s);
						}
						break;
					case ES_ARROW_LEFT:
						if (cursor > 0) {
							cursor--;
							if (history != 0) {
								s = redrawCommand(historyQueue[historyIndex].command, len, cursor, s);
							}
							else {
								s = redrawCommand(command, commandlen, cursor, s);
							}
						}
						break;
					case ES_FUNC_DELETE:
						if (history != 0 && cursor < len) {
							strcpy(command, historyQueue[historyIndex].command);
							commandlen = len;
							history = 0;
						}
						if (cursor < commandlen) {
							for (i = cursor + 1; i < commandlen; i++) {
								command[i - 1] = command[i];
							}
							commandlen--;
							s = redrawCommand(command, commandlen, cursor, s);
						}
						break;
					case ES_FUNC_HOME:
						if (cursor != 0) {
							cursor = 0;
							if (history < 0) {
								s = redrawCommand(historyQueue[historyIndex].command, len, cursor, s);
							}
							else {
								s = redrawCommand(command, commandlen, cursor, s);
							}
						}
						break;
					case ES_FUNC_END:
						if (history < 0 && cursor != len) {
							cursor = len;
							s = redrawCommand(historyQueue[historyIndex].command, len, cursor, s);
						}
						else if (cursor != commandlen) {
							cursor = commandlen;
							s = redrawCommand(command, commandlen, cursor, s);
						}
						break;
					} //switch (escSequence())
					break;
				} //switch (ch)
			} //else
		} //while ((ch = getchar()) != '\n')
		if (history != 0) {
			strcpy(command, historyQueue[historyIndex].command);
			commandlen = len;
		}
		command[commandlen] = 0;
		putchar(10);
	} //if (myshOntty)
	else {
		if (isEnd) goto command_end;

		commandlen = 0;
		while ((ch = getchar()) != '\n' && ch != 0 && ch != EOF) {
			if (commandlen < MAX_COMLEN - 1) {
				command[commandlen++] = ch;
			}
		}
		command[commandlen] = 0;

		if (ch == 0 || ch == EOF) isEnd = 1;
	} //else

	if (myshOntty) {
		ret = checkExcl(command);
		if (ret < 0 || *command == 0) goto command_start;
		else if (ret>0) puts(command);

		queueHistoryQueue(command);
	}

	while ((ret = checkAlias(command)) == 1);
	if (ret < 0 || *command == 0) goto command_start;

	ret = commandToArgs(command, command_args);
	if (ret == -1) {
		fprintf(stderr, "mysh: too many argument\n");
		goto command_start;
	}
	else if (ret == 0) goto command_start;

	nargs = expandArgs(command_args, expanded_args);
	if (nargs == -1) {
		goto command_start;
	}

	if (myshOntty) {
		ret = resetTerm();
		if (ret < 0) {
			perror("mysh: resetTerm()");
			exitShell(1);
		}
	}

	ret = checkInternal(expanded_args[0]);
	if (ret != -1) {
		ret = internalCommands(ret, nargs, expanded_args);
		if (ret < 0) {
			perror("mysh: internalCommands()");
		}
	}
	else {
		ret = externalCommands(nargs, expanded_args);
		if (ret < 0) {
			perror("mysh: externalCommands()");
		}
	}

	i = 0;
	while (expanded_args[i]) free(expanded_args[i++]);

	goto main_start;

command_end:
	if (myshOntty) {
		ret = resetTerm();
		if (ret < 0) {
			perror("mysh: resetTerm()");
			exitShell(1);
		}
	}

	exitShell(0);
	return 0;
}

////////////////////////////////////////
//INTERNAL COMMAND FUNCTIONS
////////////////////////////////////////

int mysh_exit(int argc, char* argv[]) {
	exitShell(0);
	return 0;
}

int mysh_cd(int argc, char* argv[]) {
	int ret;
	char* dirname;
	char* errstr;

	if (argc == 1) {
		if (!(dirname = getenv("HOME"))) {
			fprintf(stderr, "cd: invalid HOME directory\n");
			return 1;
		}
	}
	else if (argc == 2) dirname = argv[1];
	else {
		fprintf(stderr, "cd: too many argment\n");
		return 1;
	}

	ret = chdir(dirname);
	if (ret != 0) {
		errstr = strerror(errno);
		fprintf(stderr, "cd: %s: %s\n", dirname, errstr);
		return 1;
	}
	return 0;
}

int mysh_pushd(int argc, char* argv[]) {
	char *pchar;

	if (pDirStack < MAX_DIRS) {
		pchar = getcwd(NULL, 0);
		if (pchar) {
			dirStack[pDirStack++] = pchar;
			return 0;
		}
		else perror("pushd");
	}
	else fprintf(stderr, "pushd: directory stack is full\n");

	return 1;
}

int mysh_dirs(int argc, char* argv[]) {
	int i;

	for (i = pDirStack - 1; i >= 0; i--) {
		printf("%s\n", dirStack[i]);
	}

	return 0;
}

int mysh_popd(int argc, char* argv[]) {
	if (pDirStack > 0) {
		pDirStack--;
		if (chdir(dirStack[pDirStack]) != 0) {
			perror("popd");
			free(dirStack[pDirStack]);
			return 1;
		}
		else {
			free(dirStack[pDirStack]);
			return 0;
		}
	}
	else fprintf(stderr, "popd: directory stack is empty\n");

	return 1;
}

int mysh_history(int argc, char* argv[]) {
	int i;

	for (i = 0; i < nHistoryQueue; i++) {
		printf("%5d %s\n",
			historyQueue[(startHistoryQueue + i) % MAX_HISTORIES].num,
			historyQueue[(startHistoryQueue + i) % MAX_HISTORIES].command);
	}

	return 0;
}

int mysh_prompt(int argc, char* argv[]) {
	if (argc == 1) {
		strcpy(prompt, "prompt");
		return 0;
	}
	else if (argc != 2) {
		fprintf(stderr, "prompt: invalid argument\n");
		return 1;
	}
	if (strlen(argv[1]) >= MAX_PROMPTLEN) {
		fprintf(stderr, "prompt: string is too long\n");
		return 1;
	}
	strcpy(prompt, argv[1]);
	return 0;
}

int mysh_alias(int argc, char* argv[]) {
	struct ALIAS* alias;

	if (argc == 1) {
		alias = aliasList;
		while (alias) {
			printf("%s %s\n", alias->alias, alias->command);
			alias = alias->next;
		}
		return 0;
	}
	else if (argc != 3) {
		fprintf(stderr, "alias: invalid argument\n");
		return 1;
	}

	alias = aliasList;
	while (alias) {
		if (strcmp(alias->alias, argv[1]) == 0) {
			fprintf(stderr, "alias: %s: already exists\n", argv[1]);
			return 1;
		}
		alias = alias->next;
	}

	if (!(alias = malloc(sizeof(struct ALIAS)))) {
		goto err_alias;
	}
	else if (!(alias->alias = strdup(argv[1]))) {
		goto err_alias_alias;
	}
	else if (!(alias->command = strdup(argv[2]))) {
		goto err_alias_command;
	}
	alias->next = aliasList;
	aliasList = alias;
	return 0;

err_alias_command:
	free(alias->alias);
err_alias_alias:
	free(alias);
err_alias:
	fprintf(stderr, "alias: alias registration failed\n");
	return 1;
}

int mysh_unalias(int argc, char* argv[]) {
	struct ALIAS* alias;
	struct ALIAS* prev;

	if (argc != 2) {
		fprintf(stderr, "unalias: invalid argument\n");
		return 1;
	}

	prev = 0;
	alias = aliasList;
	while (alias) {
		if (strcmp(alias->alias, argv[1]) == 0) {
			if (prev) prev->next = alias->next;
			else aliasList = alias->next;
			free(alias->alias);
			free(alias->command);
			free(alias);
			return 0;
		}
		prev = alias;
		alias = alias->next;
	}

	fprintf(stderr, "unalias: %s: unregistered alias\n", argv[1]);
	return 1;
}

int mysh_lock(int argc, char* argv[]) {
	int i, ret, locked;
	struct termios set, reset;
	char password[21];
	char typed[21];

	ret = tcgetattr(0, &reset);
	if (ret != 0) {
		perror("lock");
		return 1;
	}

	set = reset;
	set.c_lflag &= ~(ICANON | ECHO);
	ret = tcsetattr(0, TCSANOW, &set);
	if (ret != 0) {
		perror("lock");
		return 1;
	}

	*password = 0;
	locked = 0;
	while (!locked) {
		if (*password == 0) fputs("Set password: ", stdout);
		else fputs("Check password: ", stdout);

		i = 0;
		while ((ret = getchar()) != '\n') {
			if (isgraph(ret) && i < 20) {
				typed[i++] = ret;
				putchar('*');
			}
			else if ((ret == 8 || ret == 127) && i > 0) {
				i--;
				fputs("\b \b", stdout);
			}
			else if (ret == 0 || ret == EOF) {
				fprintf(stderr, "\nlock: unexpected end of stream\n");
				goto unlock;
			}
		}
		if (i == 0 && *password == 0) {
			fprintf(stderr, "\nlock: password lock canceled\n");
			goto unlock;
		}
		putchar(10);
		typed[i] = 0;

		if (*password == 0) {
			strcpy(password, typed);
		}
		else if (strcmp(password, typed) == 0) locked = 1;
		else {
			puts("Password check failed");
			*password = 0;
		}
	}

	set.c_lflag &= ~(ISIG);
	ret = tcsetattr(0, TCSANOW, &set);
	if (ret != 0) {
		perror("lock");
		return 1;
	}

	system("clear");

	for (;;) {
		fputs("Password: ", stdout);

		i = 0;
		while ((ret = getchar()) != '\n') {
			if (isgraph(ret) && i < 20) {
				typed[i++] = ret;
				putchar('*');
			}
			else if ((ret == 8 || ret == 127) && i > 0) {
				i--;
				fputs("\b \b", stdout);
			}
		}
		putchar(10);
		typed[i] = 0;

		if (strcmp(password, typed) == 0) {
			puts("Unlocked");
			break;
		}
		else {
			puts("Invalid password");
		}
	}

unlock:
	ret = tcsetattr(0, TCSANOW, &reset);
	if (ret != 0) {
		perror("lock");
		return 1;
	}

	if (locked) return 0;
	else return 1;
}

int mysh_ver(int argc, char* argv[]) {
	puts(mysh_version);
	return 0;
}

////////////////////////////////////////
//FUNCTION initSignal
//FUNCTION resetSignal
////////////////////////////////////////

void initSignal(void) {
	signal(SIGINT, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
}

void resetSignal(void) {
	signal(SIGINT, SIG_DFL);
	signal(SIGQUIT, SIG_DFL);
}

////////////////////////////////////////
//FUNCTION initTerm
//FUNCTION resetTerm
////////////////////////////////////////

int initTerm(void) {
	int ret;

	ret = tcgetattr(0, &old);
	if (ret != 0) return -1;

	cur = old;
	cur.c_lflag &= ~(ICANON | ECHO | ISIG);
	ret = tcsetattr(0, TCSANOW, &cur);
	if (ret != 0) return -1;

	return 0;
}

int resetTerm(void) {
	int ret;

	ret = tcsetattr(0, TCSANOW, &old);
	if (ret != 0) return -1;

	return 0;
}

////////////////////////////////////////
//FUNCTION escSequence
////////////////////////////////////////

int escSequence(void) {
	switch (getchar()) {
	case '[':
		switch (getchar()) {
		case 'A':
			return ES_ARROW_UP;
		case 'B':
			return ES_ARROW_DOWN;
		case 'C':
			return ES_ARROW_RIGHT;
		case 'D':
			return ES_ARROW_LEFT;
		case '3':
			if (getchar() == '~') return ES_FUNC_DELETE;
			break;
		}
		break;
	case 'O':
		switch (getchar()) {
		case 'H':
			return ES_FUNC_HOME;
		case 'F':
			return ES_FUNC_END;
		}
		break;
	}

	return ES_NO_SEQ;
}

////////////////////////////////////////
//FUNCTION checkExcl
//FUNCTION checkAlias
////////////////////////////////////////

int checkExcl(char* command) {
	char* pchar;
	char* history;
	char* tmp;
	int historyIndex;
	int len, historylen, count = 0;

	pchar = command;
	while (*pchar == ' ' || *pchar == '\t') pchar++;
	strcpy(command, pchar);

	pchar = command;
	while (*pchar != 0) {
		if (*pchar == '!') {
			if (*(pchar + 1) == '!') {
				len = 2;
				historyIndex = checkHistoryQueue(-1, NULL, 0);
			}
			else {
				len = strtol(pchar + 1, &tmp, 10);
				if ((pchar + 1) != tmp) {
					historyIndex = checkHistoryQueue(len, NULL, 0);
					len = tmp - pchar;
				}
				else {
					len = 1;
					while (*(pchar + len) != ' '&&*(pchar + len) != 0 && *(pchar + len) != '\t') len++;
					historyIndex = checkHistoryQueue(0, pchar + 1, len - 1);
				}
			}
			if (historyIndex == -1) {
				fprintf(stderr, "mysh: history not found\n");
				return -1;
			}
			else {
				history = historyQueue[historyIndex].command;
				if ((historylen = strlen(history)) + strlen(command) - len < MAX_COMLEN) {
					tmp = strdup(pchar + len);
					if (!tmp) {
						perror("mysh: checkExcl()");
						return -1;
					}
					strcpy(pchar, history);
					strcpy(pchar + historylen, tmp);
					free(tmp);
					count++;
				}
				else {
					fprintf(stderr, "mysh: command is too long\n");
					return -1;
				}
			}
			pchar += historylen;
		} //if (*pchar == '!')
		else pchar++;
	} //while (*pchar != 0)

	return count;
}

int checkAlias(char* command) {
	char* pchar;
	char* aliasCommand;
	char* tmp;
	struct ALIAS* alias;
	int len, aliaslen;

	pchar = command;
	while (*pchar == ' ' || *pchar == '\t') pchar++;
	strcpy(command, pchar);

	pchar = command;
	while (*pchar != ' '&& *pchar != '\t' && *pchar != 0) pchar++;
	len = pchar - command;

	alias = aliasList;
	while (alias) {
		if (strlen(alias->alias) == len && strncmp(alias->alias, command, len) == 0) {
			break;
		}
		alias = alias->next;
	}

	if (alias) {
		aliasCommand = alias->command;
		if ((aliaslen = strlen(aliasCommand)) + strlen(command) - len < MAX_COMLEN) {
			tmp = strdup(pchar);
			if (!tmp) {
				perror("mysh: checkAlias()");
				return -1;
			}
			strcpy(command, aliasCommand);
			strcpy(command + aliaslen, tmp);
			free(tmp);
			return 1;
		}
		else {
			fprintf(stderr, "mysh: command is too long\n");
			return -1;
		}
	}

	return 0;
}

////////////////////////////////////////
//FUNCTION commandToArgs
//FUNCTION expandArgs
////////////////////////////////////////

int commandToArgs(char* command, char* command_args[]) {
	int pargs = 0;
	char* pchar = command;

	while (*pchar != 0) {
		while (*pchar == ' ' || *pchar == '\t') *(pchar++) = 0;
		if (*pchar == 0) break;

		if (pargs < MAX_ARGLEN - 1) command_args[pargs++] = pchar;
		else return -1;

		while (*pchar != 0 && *pchar != ' ' && *pchar != '\t') pchar++;
	}
	command_args[pargs] = 0;

	return pargs;
}

int expandArgs(char* command_args[], char* expanded_args[]) {
	int len = 0;
	char* pchar;

	foreground = 1;

	while (*command_args) {
		if ((haveChar(*command_args, '*') || haveChar(*command_args, '?')) && !haveChar(*command_args, '/')) {
			DIR* pd;
			struct dirent* files;
			char* cwd;
			int count = 0;

			cwd = getcwd(NULL, 0);
			if (!cwd) goto syscall_error;
			pd = opendir(cwd);
			if (!pd) {
				free(cwd);
				goto syscall_error;
			}

			while ((files = readdir(pd))) {
				if (fnmatch(*command_args, files->d_name, FNM_PERIOD) != 0) continue;
				else count++;

				if (!(pchar = strdup(files->d_name))) {
					closedir(pd);
					free(cwd);
					goto syscall_error;
				}
				if (len < MAX_ARGLEN - 1) expanded_args[len++] = pchar;
				else {
					free(pchar);
					closedir(pd);
					free(cwd);
					fprintf(stderr, "mysh: too many argument\n");
					goto error;
				}
			}

			closedir(pd);
			free(cwd);

			if (count == 0) {
				if (!(pchar = strdup(*command_args))) goto syscall_error;
				if (len < MAX_ARGLEN - 1) expanded_args[len++] = pchar;
				else {
					free(pchar);
					fprintf(stderr, "mysh: too many argument\n");
					goto error;
				}
			}
		} //if ((haveChar(*command_args, '*') || haveChar(*command_args, '?')) && !haveChar(*command_args, '/'))
		else if (*(*command_args) == '~') {
			char* homedir;

			if ((*(*command_args + 1) == 0 || *(*command_args + 1) == '/') && (homedir = getenv("HOME"))) {
				if (!(pchar = malloc(strlen(homedir) + strlen(*command_args + 1) + 1))) {
					fprintf(stderr, "mysh: expandArgs(): malloc failed\n");
					goto error;
				}
				strcpy(pchar, homedir);
				strcat(pchar, *command_args + 1);
			}
			else if (!(pchar = strdup(*command_args))) goto syscall_error;

			if (len < MAX_ARGLEN - 1) expanded_args[len++] = pchar;
			else {
				free(pchar);
				fprintf(stderr, "mysh: too many argument\n");
				goto error;
			}
		} //else if (*(*command_args) == '~')
		else if (strcmp(*command_args, "&") == 0) {
			if (len == 0 || *(command_args + 1) != 0) {
				fprintf(stderr, "mysh: syntax error \'&\'\n");
				goto error;
			}
			else foreground = 0;
		} //else if (strcmp(*command_args, "&") == 0)
		else {
			if (!(pchar = strdup(*command_args))) goto syscall_error;
			if (len < MAX_ARGLEN - 1) expanded_args[len++] = pchar;
			else {
				free(pchar);
				fprintf(stderr, "mysh: too many argument\n");
				goto error;
			}
		} //else
		command_args++;
	} //while (*command_args)
	expanded_args[len] = 0;

	return len;

syscall_error:
	perror("mysh: expandArgs()");
error:
	while (len > 0) {
		free(expanded_args[--len]);
	}
	return -1;
}

////////////////////////////////////////
//FUNCTION checkInternal
////////////////////////////////////////

int checkInternal(char* name) {
	int index;
	for (index = 0; index < nCommands; index++) {
		if (strcmp(commands[index].name, name) == 0) return index;
	}
	return -1;
}

////////////////////////////////////////
//FUNCTION internalCommands
//FUNCTION externalCommands
////////////////////////////////////////

int internalCommands(int index, int argc, char* command_args[]) {
	if (!foreground) {
		pid_t child;

		child = fork();
		if (child == -1) return -1;
		else if (child == 0) {
			if (myshOntty) resetSignal();
			exit(commands[index].func(argc, command_args));
		}

		return 0;
	}
	else return commands[index].func(argc, command_args);
}

int externalCommands(int argc, char* command_args[]) {
	pid_t child;
	int stat;
	char* errstr;

	child = fork();
	if (child == -1) return -1;
	else if (child == 0) {
		if (myshOntty) resetSignal();
		execvp(command_args[0], command_args);
		errstr = strerror(errno);
		fprintf(stderr, "mysh: %s: %s\n", command_args[0], errstr);
		exit(0);
	}

	if (foreground) waitpid(child, &stat, 0);

	return 0;
}

////////////////////////////////////////
//FUNCTION initHistoryQueue
//FUNCTION queueHistoryQueue
//FUNCTION checkHistoryQueue
//FUNCTION saveHistoryQueue
////////////////////////////////////////

void initHistoryQueue(void) {
	FILE* hf;
	char command[MAX_COMLEN];
	char* homedir;
	int ch, pcommand;

	homedir = getenv("HOME");
	if (!homedir) return;

	if (snprintf(command, MAX_COMLEN, "%s/.mysh_history", homedir) < 0) return;
	if ((hf = fopen(command, "r")) == NULL) return;

	pcommand = 0;
	while ((ch = fgetc(hf)) != EOF && pcommand < MAX_COMLEN) {
		if (ch == '\n') {
			if (pcommand == 0) continue;
			command[pcommand] = 0;
			queueHistoryQueue(command);
			pcommand = 0;
		}
		else command[pcommand++] = ch;
	}

	fclose(hf);
}

void queueHistoryQueue(char* command) {
	char* pchar;
	int num;

	if ((pchar = strdup(command)) == NULL) {
		perror("mysh: queueHistoryQueue()");
		return;
	}

	if (nHistoryQueue == 0) num = 1;
	else num = historyQueue[(startHistoryQueue + nHistoryQueue - 1) % MAX_HISTORIES].num + 1;

	if (nHistoryQueue < MAX_HISTORIES) {
		historyQueue[(startHistoryQueue + nHistoryQueue) % MAX_HISTORIES].command = pchar;
		historyQueue[(startHistoryQueue + nHistoryQueue) % MAX_HISTORIES].num = num;
		nHistoryQueue++;
	}
	else {
		free(historyQueue[startHistoryQueue].command);
		historyQueue[startHistoryQueue].command = pchar;
		historyQueue[startHistoryQueue].num = num;
		startHistoryQueue = (startHistoryQueue + 1) == MAX_HISTORIES ? 0 : (startHistoryQueue + 1);
	}
}

int checkHistoryQueue(int num, char* string, int len) {
	if (nHistoryQueue == 0) return -1;

	if (num > 0) {
		int startnum;

		startnum = historyQueue[startHistoryQueue].num;
		if (num >= startnum && num < startnum + nHistoryQueue) {
			return (startHistoryQueue + (num - startnum)) % MAX_HISTORIES;
		}
	}
	else if (num < 0) {
		if (nHistoryQueue + num >= 0) {
			return (startHistoryQueue + nHistoryQueue + num) % MAX_HISTORIES;
		}
	}
	else if (string && len>0) {
		int i;

		for (i = nHistoryQueue - 1; i >= 0; i--) {
			if (strncmp(historyQueue[(startHistoryQueue + i) % MAX_HISTORIES].command, string, len) == 0) {
				return (startHistoryQueue + i) % MAX_HISTORIES;
			}
		}
	}

	return -1;
}

void saveHistoryQueue(void) {
	FILE* hf;
	char command[MAX_COMLEN];
	char* homedir;
	int i;

	homedir = getenv("HOME");
	if (!homedir) return;

	if (snprintf(command, MAX_COMLEN, "%s/.mysh_history", homedir) < 0) return;
	if ((hf = fopen(command, "w")) == NULL) return;

	for (i = 0; i < nHistoryQueue; i++) {
		fprintf(hf, "%s\n", historyQueue[(startHistoryQueue + i) % MAX_HISTORIES].command);
		free(historyQueue[(startHistoryQueue + i) % MAX_HISTORIES].command);
	}

	fclose(hf);
}

////////////////////////////////////////
//FUNCTION freeAliasList
////////////////////////////////////////

void freeAliasList(void) {
	struct ALIAS* alias = aliasList;
	struct ALIAS* next;
	while (alias) {
		next = alias->next;
		free(alias->alias);
		free(alias->command);
		free(alias);
		alias = next;
	}
}

////////////////////////////////////////
//SOME OTHER FUNCTIONS
//FUNCTION exitShell
//FUNCTION haveChar
//FUNCTION redrawCommand
////////////////////////////////////////

void exitShell(int exitcode) {
	if (myshOntty) {
		saveHistoryQueue();
	}
	freeAliasList();
	while (pDirStack > 0) {
		free(dirStack[--pDirStack]);
	}
	exit(exitcode);
}

int haveChar(char* string, char ch) {
	while (*string) {
		if (*string == ch) return 1;
		string++;
	}
	return 0;
}

int redrawCommand(char* command, int len, int cursor, int s) {
	struct winsize wsz;
	int ret, i, commandlen;

	ret = ioctl(0, TIOCGWINSZ, &wsz);
	if (ret < 0) return s;

	commandlen = wsz.ws_col - strlen(prompt) - 2; //in freebsd, .. -3;

	if (commandlen > 0) {
		if (commandlen >= len) {
			s = 0;
			putchar(13);
			fputs(prompt, stdout);
			putchar(' ');
			for (i = 0; i < len; i++) {
				putchar(command[i]);
			}
			for (; i < commandlen + 1; i++) {
				putchar(' ');
			}
			for (i = commandlen; i >= cursor; i--) {
				putchar('\b');
			}
		}
		else {
			if (len - s < commandlen) s = len - commandlen;
			if (cursor < s) s = cursor;
			else if(cursor > s + commandlen) s = cursor - commandlen;
			putchar(13);
			fputs(prompt, stdout);
			if (s > 0) putchar('<');
			else putchar(' ');
			for (i = 0; i < commandlen; i++) {
				putchar(command[s + i]);
			}
			if (len > s + commandlen) putchar('>');
			else putchar(' ');
			for (i = s + commandlen; i >= cursor; i--) {
				putchar('\b');
			}
		}
	} //if (commandlen > 0)
	else {
		putchar(13);
		for (i = 0; i < wsz.ws_col && prompt[i]; i++) {
			putchar(prompt[i]);
		}
		if (i < wsz.ws_col) {
			putchar(' ');
			i++;
			for (; i < wsz.ws_col; i++) putchar('#');
		}
	} //else

	return s;
}
