#if !defined(_QNX_SCREEN_EVENT_MONITOR_H_)
#define _QNX_SCREEN_EVENT_MONITOR_H_

#include <screen/screen.h>

struct qnx_screen_event_monitor {
	screen_context_t context;
	int chid;
	int coid;
	pthread_t thread;
	int pipe_fds[2];
	struct sigevent event;
};

struct qnx_screen_event_monitor *
qnx_screen_event_monitor_create(screen_context_t context);
void
qnx_screen_event_monitor_destroy(struct qnx_screen_event_monitor *m);
void
qnx_screen_event_monitor_arm(struct qnx_screen_event_monitor *m);


#endif // !defined(_QNX_SCREEN_EVENT_MONITOR_H_)

