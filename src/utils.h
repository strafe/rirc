#ifndef UTILS_H
#define UTILS_H

#include <errno.h>

/* Parsed IRC message */
typedef struct parsed_mesg
{
	char *from;
	char *hostinfo;
	char *command;
	char *params;
	char *trailing;
} parsed_mesg;

char* getarg(char**, const char*);
char* strdup(const char*);
char* word_wrap(int, char**, char*);

int check_pinged(const char*, const char*);
parsed_mesg* parse(parsed_mesg*, char*);
void error(int status, const char*, ...);

/* Irrecoverable error
 *   this define is precluded in test.h to aggregate fatal errors in testcases */
#ifndef fatal
#define fatal(mesg) \
	do { error(errno, "ERROR in %s: %s", __func__, mesg); } while (0)
#endif

//TODO: refactor
/* Doubly linked list macros */
#define DLL_NEW(L, N) ((L) = (N)->next = (N)->prev = (N))

#define DLL_ADD(L, N) \
	do { \
		if ((L) == NULL) \
			DLL_NEW(L, N); \
		else { \
			((L)->next)->prev = (N); \
			(N)->next = ((L)->next); \
			(N)->prev = (L); \
			(L)->next = (N); \
		} \
	} while (0)

#define DLL_DEL(L, N) \
	do { \
		if (((N)->next) == (N)) \
			(L) = NULL; \
		else { \
			if ((L) == N) \
				(L) = ((N)->next); \
			((N)->next)->prev = ((N)->prev); \
			((N)->prev)->next = ((N)->next); \
		} \
	} while (0)


#endif
