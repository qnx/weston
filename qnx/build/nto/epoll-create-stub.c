#include <errno.h>
#include <stdio.h>
#include <sys/epoll.h>

// Some things don't use epoll but they link to the shared or
// shared-cairo libraries which contain a wrapper around
// epoll_create/epoll_create1 in os-compatibility.c.  Stub
// out epoll_create* to avoid the unnecessary code
// inclusion/linkage.

// Another solution explored for this problem was the use of
// -ffunction-sections and -fdata-sections to compile and
// --gc-sections to link.  This results in the removal of
// unused functions.  At the time, it also resulted in the
// linker complaining "DSO missing from command line".  It
// appears that the processing for "DSO missing" takes place
// before the processing for --gc-sections.  The "DSO
// missing" message indicates that the object being linked
// references a function that can only be found indirectly;
// e.g., the object being linked needs shared object A, A
// needs shared object B, B defines function, the object
// being linked isn't linked to B.  If you make the object
// being linked require B, the link succeeds and the
// functions are eliminated but the requirement is not
// even though it is no longer necessary after the functions
// are eliminated.  Given this problem and the fact that
// function/data sections are not without cost (see GCC
// documentation), it's probably better to just stub out
// the functions where they aren't actually needed.

int __attribute__ ((visibility("hidden")))
epoll_create(int size)
{
	fprintf(stderr, "epoll_create stub called\n");
	errno = ENOSYS;
	return -1;
}

int __attribute__ ((visibility("hidden")))
epoll_create1(int flags)
{
	fprintf(stderr, "epoll_create1 stub called\n");
	errno = ENOSYS;
	return -1;
}

#if defined(__QNXNTO__) && defined(__USESRCVERSION)
#include <sys/srcversion.h>
__SRCVERSION("$URL: http://f27svn.qnx.com/svn/repos/osr/trunk/weston/build/nto/epoll-create-stub.c $ $Rev: 2316 $")
#endif
