#ifndef NET_H
#define NET_H

#include "server.h"

#define RECONNECT_DELTA 15

/* TODO: refactoring */
int sendf(char*, server*, const char*, ...);
server* get_server_head(void);
void check_servers(void);
void server_connect(char*, char*, char*, char*);
void server_disconnect(server*, int, int, char*);

#endif
