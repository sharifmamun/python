/***********************************************************
Copyright 1991-1995 by Stichting Mathematisch Centrum, Amsterdam,
The Netherlands.

                        All Rights Reserved

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose and without fee is hereby granted,
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in
supporting documentation, and that the names of Stichting Mathematisch
Centrum or CWI or Corporation for National Research Initiatives or
CNRI not be used in advertising or publicity pertaining to
distribution of the software without specific, written prior
permission.

While CWI is the initial source for this software, a modified version
is made available by the Corporation for National Research Initiatives
(CNRI) at the Internet address ftp://ftp.python.org.

STICHTING MATHEMATISCH CENTRUM AND CNRI DISCLAIM ALL WARRANTIES WITH
REGARD TO THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL STICHTING MATHEMATISCH
CENTRUM OR CNRI BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL
DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
PERFORMANCE OF THIS SOFTWARE.

******************************************************************/

/* This code implemented by Dag.Gruneau@elsa.preseco.comm.se */
/* Fast NonRecursiveMutex support by Yakov Markovitch, markovitch@iso.ru */

#include <windows.h>
#include <limits.h>
#include <process.h>

typedef struct NRMUTEX {
	LONG   owned ;
	DWORD  thread_id ;
	HANDLE hevent ;
} NRMUTEX, *PNRMUTEX ;


typedef PVOID WINAPI interlocked_cmp_xchg_t(PVOID *dest, PVOID exc, PVOID comperand) ;

/* Sorry mate, but we haven't got InterlockedCompareExchange in Win95! */
static PVOID WINAPI interlocked_cmp_xchg(PVOID *dest, PVOID exc, PVOID comperand)
{
	static LONG spinlock = 0 ;
	PVOID result ;

	/* Acqire spinlock (yielding control to other threads if cant aquire for the moment) */
	while(InterlockedExchange(&spinlock, 1)) Sleep(0) ;
	result = *dest ;
	if (result == comperand)
		*dest = exc ;
	/* Release spinlock */
	spinlock = 0 ;
	return result ;
} ;

static interlocked_cmp_xchg_t *ixchg ;
BOOL InitializeNonRecursiveMutex(PNRMUTEX mutex)
{
	if (!ixchg)
	{
		/* Sorely, Win95 has no InterlockedCompareExchange API (Win98 has), so we have to use emulation */
		HANDLE kernel = GetModuleHandle("kernel32.dll") ;
		if (!kernel || (ixchg = (interlocked_cmp_xchg_t *)GetProcAddress(kernel, "InterlockedCompareExchange")) == NULL)
			ixchg = interlocked_cmp_xchg ;
	}

	mutex->owned = -1 ;  /* No threads have entered NonRecursiveMutex */
	mutex->thread_id = 0 ;
	mutex->hevent = CreateEvent(NULL, FALSE, FALSE, NULL) ;
	return mutex->hevent != NULL ;	/* TRUE if the mutex is created */
}

#define InterlockedCompareExchange(dest,exchange,comperand) (ixchg((dest), (exchange), (comperand)))

VOID DeleteNonRecursiveMutex(PNRMUTEX mutex)
{
	/* No in-use check */
	CloseHandle(mutex->hevent) ;
	mutex->hevent = NULL ; /* Just in case */
}

DWORD EnterNonRecursiveMutex(PNRMUTEX mutex, BOOL wait)
{
	/* Assume that the thread waits successfully */
	DWORD ret ;

	/* InterlockedIncrement(&mutex->owned) == 0 means that no thread currently owns the mutex */
	if (!wait)
	{
		if (InterlockedCompareExchange((PVOID *)&mutex->owned, (PVOID)0, (PVOID)-1) != (PVOID)-1)
			return WAIT_TIMEOUT ;
		ret = WAIT_OBJECT_0 ;
	}
	else
		ret = InterlockedIncrement(&mutex->owned) ?
			/* Some thread owns the mutex, let's wait... */
			WaitForSingleObject(mutex->hevent, INFINITE) : WAIT_OBJECT_0 ;

	mutex->thread_id = GetCurrentThreadId() ; /* We own it */
	return ret ;
}

BOOL LeaveNonRecursiveMutex(PNRMUTEX mutex)
{
	/* We don't own the mutex */
	mutex->thread_id = 0 ;
	return
		InterlockedDecrement(&mutex->owned) < 0 ||
		SetEvent(mutex->hevent) ; /* Other threads are waiting, wake one on them up */
}

PNRMUTEX AllocNonRecursiveMutex()
{
	PNRMUTEX mutex = (PNRMUTEX)malloc(sizeof(NRMUTEX)) ;
	if (mutex && !InitializeNonRecursiveMutex(mutex))
	{
		free(mutex) ;
		mutex = NULL ;
	}
	return mutex ;
}

void FreeNonRecursiveMutex(PNRMUTEX mutex)
{
	if (mutex)
	{
		DeleteNonRecursiveMutex(mutex) ;
		free(mutex) ;
	}
}

long PyThread_get_thread_ident(void);

/*
 * Change all headers to pure ANSI as no one will use K&R style on an
 * NT
 */

/*
 * Initialization of the C package, should not be needed.
 */
static void PyThread__init_thread(void)
{
}

/*
 * Thread support.
 */
int PyThread_start_new_thread(void (*func)(void *), void *arg)
{
	long rv;
	int success = 0;

	dprintf(("%ld: PyThread_start_new_thread called\n", PyThread_get_thread_ident()));
	if (!initialized)
		PyThread_init_thread();

	rv = _beginthread(func, 0, arg); /* use default stack size */
 
	if (rv != -1) {
		success = 1;
		dprintf(("%ld: PyThread_start_new_thread succeeded: %ld\n", PyThread_get_thread_ident(), aThreadId));
	}

	return success;
}

/*
 * Return the thread Id instead of an handle. The Id is said to uniquely identify the
 * thread in the system
 */
long PyThread_get_thread_ident(void)
{
	if (!initialized)
		PyThread_init_thread();

	return GetCurrentThreadId();
}

static void do_PyThread_exit_thread(int no_cleanup)
{
	dprintf(("%ld: PyThread_exit_thread called\n", PyThread_get_thread_ident()));
	if (!initialized)
		if (no_cleanup)
			_exit(0);
		else
			exit(0);
	_endthread();
}

void PyThread_exit_thread(void)
{
	do_PyThread_exit_thread(0);
}

void PyThread__exit_thread(void)
{
	do_PyThread_exit_thread(1);
}

#ifndef NO_EXIT_PROG
static void do_PyThread_exit_prog(int status, int no_cleanup)
{
	dprintf(("PyThread_exit_prog(%d) called\n", status));
	if (!initialized)
		if (no_cleanup)
			_exit(status);
		else
			exit(status);
}

void PyThread_exit_prog(int status)
{
	do_PyThread_exit_prog(status, 0);
}

void PyThread__exit_prog _P1(int status)
{
	do_PyThread_exit_prog(status, 1);
}
#endif /* NO_EXIT_PROG */

/*
 * Lock support. It has too be implemented as semaphores.
 * I [Dag] tried to implement it with mutex but I could find a way to
 * tell whether a thread already own the lock or not.
 */
PyThread_type_lock PyThread_allocate_lock(void)
{
	PNRMUTEX aLock;

	dprintf(("PyThread_allocate_lock called\n"));
	if (!initialized)
		PyThread_init_thread();

	aLock = AllocNonRecursiveMutex() ;

	dprintf(("%ld: PyThread_allocate_lock() -> %lx\n", PyThread_get_thread_ident(), (long)aLock));

	return (PyThread_type_lock) aLock;
}

void PyThread_free_lock(PyThread_type_lock aLock)
{
	dprintf(("%ld: PyThread_free_lock(%lx) called\n", PyThread_get_thread_ident(),(long)aLock));

	FreeNonRecursiveMutex(aLock) ;
}

/*
 * Return 1 on success if the lock was acquired
 *
 * and 0 if the lock was not acquired. This means a 0 is returned
 * if the lock has already been acquired by this thread!
 */
int PyThread_acquire_lock(PyThread_type_lock aLock, int waitflag)
{
	int success ;

	dprintf(("%ld: PyThread_acquire_lock(%lx, %d) called\n", PyThread_get_thread_ident(),(long)aLock, waitflag));

	success = aLock && EnterNonRecursiveMutex((PNRMUTEX) aLock, (waitflag == 1 ? INFINITE : 0)) == WAIT_OBJECT_0 ;

	dprintf(("%ld: PyThread_acquire_lock(%lx, %d) -> %d\n", PyThread_get_thread_ident(),(long)aLock, waitflag, success));

	return success;
}

void PyThread_release_lock(PyThread_type_lock aLock)
{
	dprintf(("%ld: PyThread_release_lock(%lx) called\n", PyThread_get_thread_ident(),(long)aLock));

	if (!(aLock && LeaveNonRecursiveMutex((PNRMUTEX) aLock)))
		dprintf(("%ld: Could not PyThread_release_lock(%lx) error: %l\n", PyThread_get_thread_ident(), (long)aLock, GetLastError()));
}

/*
 * Semaphore support.
 */
PyThread_type_sema PyThread_allocate_sema(int value)
{
	HANDLE aSemaphore;

	dprintf(("%ld: PyThread_allocate_sema called\n", PyThread_get_thread_ident()));
	if (!initialized)
		PyThread_init_thread();

	aSemaphore = CreateSemaphore( NULL,           /* Security attributes          */
	                              value,          /* Initial value                */
	                              INT_MAX,        /* Maximum value                */
	                              NULL);          /* Name of semaphore            */

	dprintf(("%ld: PyThread_allocate_sema() -> %lx\n", PyThread_get_thread_ident(), (long)aSemaphore));

	return (PyThread_type_sema) aSemaphore;
}

void PyThread_free_sema(PyThread_type_sema aSemaphore)
{
	dprintf(("%ld: PyThread_free_sema(%lx) called\n", PyThread_get_thread_ident(), (long)aSemaphore));

	CloseHandle((HANDLE) aSemaphore);
}

/*
  XXX must do something about waitflag
 */
int PyThread_down_sema(PyThread_type_sema aSemaphore, int waitflag)
{
	DWORD waitResult;

	dprintf(("%ld: PyThread_down_sema(%lx) called\n", PyThread_get_thread_ident(), (long)aSemaphore));

	waitResult = WaitForSingleObject( (HANDLE) aSemaphore, INFINITE);

	dprintf(("%ld: PyThread_down_sema(%lx) return: %l\n", PyThread_get_thread_ident(),(long) aSemaphore, waitResult));
	return 0;
}

void PyThread_up_sema(PyThread_type_sema aSemaphore)
{
	ReleaseSemaphore(
                (HANDLE) aSemaphore,            /* Handle of semaphore                          */
                1,                              /* increment count by one                       */
                NULL);                          /* not interested in previous count             */
                                                
	dprintf(("%ld: PyThread_up_sema(%lx)\n", PyThread_get_thread_ident(), (long)aSemaphore));
}
