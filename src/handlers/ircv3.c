#include <string.h>

#include "src/handlers/ircv3.h"
#include "src/io.h"
#include "src/state.h"

#define failf(S, ...) \
	do { server_error((S), __VA_ARGS__); \
	     return 1; \
	} while (0)

#define sendf(S, ...) \
	do { int ret; \
	     if ((ret = io_sendf((S)->connection, __VA_ARGS__))) \
	         failf((S), "Send fail: %s", io_err(ret)); \
	} while (0)

#define IRCV3_RECV_HANDLERS \
	X(LIST) \
	X(LS) \
	X(ACK) \
	X(NAK) \
	X(DEL) \
	X(NEW)

#define X(CMD) \
static int ircv3_recv_cap_##CMD(struct server*, struct irc_message*);
IRCV3_RECV_HANDLERS
#undef X

int
ircv3_recv_CAP(struct server *s, struct irc_message *m)
{
	char *targ;
	char *cmnd;

	if (!irc_message_param(m, &targ))
		failf(s, "CAP: target is null");

	if (!irc_message_param(m, &cmnd))
		failf(s, "CAP: command is null");

	#define X(CMD) \
	if (!strcmp(cmnd, #CMD)) \
		return ircv3_recv_cap_##CMD(s, m);
	IRCV3_RECV_HANDLERS
	#undef X

	failf(s, "CAP: unrecognized subcommand '%s'", cmnd);
}

static int
ircv3_recv_cap_LS(struct server *s, struct irc_message *m)
{
	/* If no capabilities are available, an empty
	 * parameter MUST be sent.
	 *
	 * Servers MAY send multiple lines in response to
	 * CAP LS and CAP LIST. If the reply contains
	 * multiple lines, all but the last reply MUST
	 * have a parameter containing only an asterisk (*)
	 * preceding the capability list
	 *
	 * CAP <targ> LS [*] :[<cap_1> [...]]
	 */

	char *cap;
	char *caps;
	char *multiline;

	irc_message_param(m, &multiline);
	irc_message_param(m, &caps);

	if (!multiline)
		failf(s, "CAP LS: parameter is null");

	if (!strcmp(multiline, "*") && !caps)
		failf(s, "CAP LS: parameter is null");

	if (strcmp(multiline, "*") && caps)
		failf(s, "CAP LS: invalid parameters");

	if (!caps) {
		caps = multiline;
		multiline = NULL;
	}

	if (s->registered) {
		server_info(s, "CAP LS: %s", (*caps ? caps : "(no capabilities)"));
		return 0;
	}

	while ((cap = strsep(&(caps)))) {
		#define X(CAP, VAR, ATTRS)                             \
		if (!strcmp(cap, CAP) && s->ircv3_caps.VAR.req_auto) { \
			s->ircv3_caps.VAR.req = 1;                         \
			s->ircv3_caps.cap_reqs++;                          \
		}
		IRCV3_CAPS
		#undef X
	}

	if (multiline)
		return 0;

	if (s->ircv3_caps.cap_reqs) {
		#define X(CAP, VAR, ATTRS)     \
		if (s->ircv3_caps.VAR.req) {   \
			s->ircv3_caps.VAR.req = 0; \
			sendf(s, "CAP REQ :" CAP); \
		}
		IRCV3_CAPS
		#undef X
	} else {
		sendf(s, "CAP END");
	}

	return 0;
}

static int
ircv3_recv_cap_LIST(struct server *s, struct irc_message *m)
{
	/* If no capabilities are available, an empty
	 * parameter MUST be sent.
	 *
	 * Servers MAY send multiple lines in response to
	 * CAP LS and CAP LIST. If the reply contains
	 * multiple lines, all but the last reply MUST
	 * have a parameter containing only an asterisk (*)
	 * preceding the capability list
	 *
	 * CAP <targ> LIST [*] :[<cap_1> [...]]
	 */

	char *caps;
	char *multiline;

	irc_message_param(m, &multiline);
	irc_message_param(m, &caps);

	if (!multiline)
		failf(s, "CAP LIST: parameter is null");

	if (multiline && caps && strcmp(multiline, "*"))
		failf(s, "CAP LIST: invalid parameters");

	if (!strcmp(multiline, "*") && !caps)
		failf(s, "CAP LIST: parameter is null");

	if (!caps)
		caps = multiline;

	server_info(s, "CAP LIST: %s", (*caps ? caps : "(no capabilities)"));

	return 0;
}

static int
ircv3_recv_cap_ACK(struct server *s, struct irc_message *m)
{
	/* Each capability name may be prefixed with a
	 * dash (-), indicating that this capability has
	 * been disabled as requested.
	 *
	 * If an ACK reply originating from the server is
	 * spread across multiple lines, a client MUST NOT
	 * change capabilities until the last ACK of the
	 * set is received. Equally, a server MUST NOT change
	 * the capabilities of the client until the last ACK
	 * of the set has been sent.
	 *
	 * CAP <targ> ACK :[-]<cap_1> [[-]<cap_2> [...]]
	 */

	char *cap;
	char *caps;
	int err;
	int errors = 0;

	if (!irc_message_param(m, &caps))
		failf(s, "CAP ACK: parameter is null");

	if (!(cap = strsep(&(caps))))
		failf(s, "CAP ACK: parameter is empty");

	do {
		if ((err = ircv3_cap_ack(&(s->ircv3_caps), cap))) {
			errors++;
			server_info(s, "capability change accepted: %s (error: %s)", cap, ircv3_cap_err(err));
		} else {
			server_info(s, "capability change accepted: %s", cap);
		}
	} while ((cap = strsep(&(caps))));

	if (errors)
		failf(s, "CAP ACK: parameter errors");

	if (!s->registered && !s->ircv3_caps.cap_reqs)
		sendf(s, "CAP END");

	return 0;
}

static int
ircv3_recv_cap_NAK(struct server *s, struct irc_message *m)
{
	/* The server MUST NOT make any change to any
	 * capabilities if it replies with a NAK subcommand.
	 *
	 * CAP <targ> NAK :<cap_1> [<cap_2> [...]]
	 */

	char *cap;
	char *caps;
	int err;
	int errors = 0;

	if (!irc_message_param(m, &caps))
		failf(s, "CAP NAK: parameter is null");

	if (!(cap = strsep(&(caps))))
		failf(s, "CAP NAK: parameter is empty");

	do {
		if ((err = ircv3_cap_nak(&(s->ircv3_caps), cap))) {
			errors++;
			server_info(s, "capability change rejected: %s (error: %s)", cap, ircv3_cap_err(err));
		} else {
			server_info(s, "capability change rejected: %s", cap);
		}
	} while ((cap = strsep(&(caps))));

	if (errors)
		failf(s, "CAP NAK: parameter errors");

	if (!s->registered && !s->ircv3_caps.cap_reqs)
		sendf(s, "CAP END");

	return 0;
}

static int
ircv3_recv_cap_DEL(struct server *s, struct irc_message *m)
{
	/* Upon receiving a CAP DEL message, the client MUST
	 * treat the listed capabilities as cancelled and no
	 * longer available. Clients SHOULD NOT send CAP REQ
	 * messages to cancel the capabilities in CAP DEL,
	 * as they have already been cancelled by the server.
	 *
	 * CAP <targ> DEL :<cap_1> [<cap_2> [...]]
	 */

	/* TODO */

	(void)s;
	(void)m;
	return 0;
}

static int
ircv3_recv_cap_NEW(struct server *s, struct irc_message *m)
{
	/* Clients that support CAP NEW messages SHOULD respond
	 * with a CAP REQ message if they wish to enable one or
	 * more of the newly-offered capabilities.
	 *
	 * CAP <targ> NEW :<cap_1> [<cap_2> [...]]
	 */

	/* TODO */

	(void)s;
	(void)m;
	return 0;
}
