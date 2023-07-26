#include "config.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/neutrino.h>
#include <unistd.h>

#include <libweston/libweston.h>
#include "qnx-screen-event-monitor.h"
#include "shared/os-compatibility.h"
#include <libweston/zalloc.h>

static int c_screenCode = _PULSE_CODE_MINAVAIL + 0;
static int c_armCode = _PULSE_CODE_MINAVAIL + 1;
static int c_quitCode = _PULSE_CODE_MINAVAIL + 2;

static void *
qnx_screen_event_monitor_main(void *argument)
{
	struct qnx_screen_event_monitor *m = argument;
	int armed = 1;
	while (1) {
		struct _pulse msg;
		memset(&msg, 0, sizeof(msg));
		int receiveId = MsgReceive(m->chid, &msg, sizeof(msg), NULL);
		if (receiveId == 0) {
			if (msg.code == c_quitCode) {
				break;
			} else if (msg.code == c_armCode) {
				armed = 1;
			} else if (msg.code == c_screenCode) {
				if (armed) {
					char ch = 0;
					write(m->pipe_fds[1], &ch, sizeof(ch));
					armed = 0;
				}
			}
		}
	}
	return NULL;
}

struct qnx_screen_event_monitor *
qnx_screen_event_monitor_create(screen_context_t context)
{
	struct qnx_screen_event_monitor *m;

	m = zalloc(sizeof *m);
	if (!m) {
		weston_log("Failed to allocate event monitor\n");
		return NULL;
	}

	m->context = context;

	m->chid = ChannelCreate(_NTO_CHF_DISCONNECT | _NTO_CHF_UNBLOCK | _NTO_CHF_PRIVATE);
	if (m->chid < 0) {
		weston_log("Failed to create channel\n");
		goto err_free;
	}

	m->coid = ConnectAttach(0, 0, m->chid, _NTO_SIDE_CHANNEL, 0);
	if (m->coid < 0) {
		weston_log("Failed to create connection\n");
		goto err_channel;
	}

	if (pipe2(m->pipe_fds, O_CLOEXEC | O_NONBLOCK) < 0) {
		weston_log("Failed to create pipe\n");
		goto err_connection;
	}

	SIGEV_PULSE_INIT(&m->event, m->coid, SIGEV_PULSE_PRIO_INHERIT, c_screenCode, 0);
	if (screen_register_event(m->context, &m->event) < 0) {
		weston_log("Failed to register event\n");
		goto err_pipe;
	}

	if (screen_notify(m->context, SCREEN_NOTIFY_EVENT, NULL, &m->event) < 0) {
		weston_log("Failed to setup notification\n");
		goto err_register;
	}

	if (pthread_create(&m->thread, NULL, qnx_screen_event_monitor_main, m) != 0) {
		goto err_notify;
	}

	return m;

err_notify:
	screen_notify(m->context, SCREEN_NOTIFY_EVENT, NULL, NULL);
err_register:
	screen_unregister_event(&m->event);
err_pipe:
	close(m->pipe_fds[0]);
	close(m->pipe_fds[1]);
err_connection:
	ConnectDetach(m->coid);
err_channel:
	ChannelDestroy(m->chid);
err_free:
	free(m);
	return NULL;
}

void
qnx_screen_event_monitor_destroy(struct qnx_screen_event_monitor *m)
{
	MsgSendPulse(m->coid, SIGEV_PULSE_PRIO_INHERIT, c_quitCode, 0);
	pthread_join(m->thread, NULL);
	screen_notify(m->context, SCREEN_NOTIFY_EVENT, NULL, NULL);
	screen_unregister_event(&m->event);
	close(m->pipe_fds[0]);
	close(m->pipe_fds[1]);
	ConnectDetach(m->coid);
	ChannelDestroy(m->chid);
	free(m);
}

void
qnx_screen_event_monitor_arm(struct qnx_screen_event_monitor *m)
{
	MsgSendPulse(m->coid, SIGEV_PULSE_PRIO_INHERIT, c_armCode, 0);
}

#if defined(__QNXNTO__) && defined(__USESRCVERSION)
#include <sys/srcversion.h>
__SRCVERSION("$URL: http://f27svn.qnx.com/svn/repos/osr/trunk/weston/build/backends/qnx-screen/qnx-screen-event-monitor.c $ $Rev: 2345 $")
#endif
