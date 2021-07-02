#include "mpl_threads.h"

#define ErrNo_To_Mpl(mResult)								\
	if ((mResult) == 0)										\
		(mResult) = MPL_SUCCESS;							\
	else if ((mResult) == ETIMEDOUT || (mResult) == EBUSY || (mResult) == EAGAIN)	\
		(mResult) = MPL_TIMEOUT;							\
	else if ((mResult) == EPERM)							\
		(mResult) = MPL_ABANDONED;							\
	else													\
		(mResult) = MPL_FAIL

#if MPL_OS_TYPE == MPL_OS_TYPE_OSX
// OS X does not have clock_gettime, use clock_get_time
static clock_serv_t g_ClkId_AbsoluteTime;
static clock_serv_t g_ClkId_RelativeTime;
#elif MPL_OS_TYPE == MPL_OS_TYPE_WINDOWS
static double g_ClockFreqPerSec;
static double g_ClockFreqPerMs;
static double g_ClockFreqPerUs;
static double g_ClockFreqPerNs;
#elif MPL_OS_TYPE == MPL_OS_TYPE_LINUX
static clockid_t g_ClkId_AbsoluteTime;
static clockid_t g_ClkId_RelativeTime;
#endif

static long g_MplInitLock = 0;

#if MPL_THREAD_TYPE == MPL_THREAD_TYPE_PTHREADS

int Mpl_Thread_Init(MPL_THREAD_T* thread_handle, MPL_THREAD_PROC_T* start_fn, void* start_arg)
{
	int r = 0;
	pthread_attr_t attr;

	if (!thread_handle) return MPL_FAIL;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	if ((r = pthread_create(&thread_handle->Handle, &attr, start_fn, start_arg)) == 0)
	{
		pthread_attr_destroy(&attr);
		return MPL_SUCCESS;
	}

	pthread_attr_destroy(&attr);
	ErrNo_To_Mpl(r);
	return r;
}
void Mpl_Thread_End(void* ret_val)
{
	pthread_exit(ret_val);
}

int Mpl_Mutex_Init(MPL_MUTEX_T* mutex_handle)
{
	int r = 0;

	if (!mutex_handle || mutex_handle->Common.Valid) return MPL_FAIL;
	if ((r = pthread_mutex_init(&mutex_handle->Handle, NULL)) == 0)
	{
		mutex_handle->Common.Valid = MPT_VALID;
		return MPL_SUCCESS;
	}

	ErrNo_To_Mpl(r);
	return r;
}

int Mpl_Mutex_Free(MPL_MUTEX_T* mutex_handle)
{
	int r = MPL_SUCCESS;
	if (!mutex_handle || mutex_handle->Common.Valid != MPT_VALID) return MPL_FAIL;
	mutex_handle->Common.Valid = 0;

	r = pthread_mutex_destroy(&mutex_handle->Handle);
	if (r != 0) goto Error;

	return MPL_SUCCESS;
Error:
	ErrNo_To_Mpl(r);
	return r;
}

int Mpl_Mutex_Wait(MPL_MUTEX_T* mutex_handle)
{
	int r = 0;
	if (!mutex_handle) return MPL_FAIL;

	r = pthread_mutex_lock(&mutex_handle->Handle);
	ErrNo_To_Mpl(r);
	return r;
}

int Mpl_Mutex_TryWait(MPL_MUTEX_T* mutex_handle)
{
	int r = 0;
	if (!mutex_handle) return MPL_FAIL;

	r = pthread_mutex_trylock(&mutex_handle->Handle);
	ErrNo_To_Mpl(r);
	return r;
}

int Mpl_Mutex_Release(MPL_MUTEX_T* mutex_handle)
{
	int r = 0;

	if (!mutex_handle) return MPL_FAIL;
	r = pthread_mutex_unlock(&mutex_handle->Handle);
	ErrNo_To_Mpl(r);
	return r;
}

int Mpl_Event_Init(MPL_EVENT_T* event_handle, int is_auto_reset, int initial_state)
{
	int r = 0;

	if (!event_handle || event_handle->Common.Valid) return MPL_FAIL;

	event_handle->IsAuto = is_auto_reset;
	event_handle->IsSet = initial_state;

	r = pthread_mutex_init(&event_handle->Handle, NULL);
	if (r == 0)
	{
		r = pthread_cond_init(&event_handle->Cond, NULL);
		if (r == 0)
		{
			event_handle->Common.Valid = MPT_VALID;
			return MPL_SUCCESS;
		}
	}

	pthread_mutex_destroy(&event_handle->Handle);
	ErrNo_To_Mpl(r);
	return r;
}

int Mpl_Event_Free(MPL_EVENT_T* event_handle)
{
	int r = MPL_SUCCESS;
	if (!event_handle || event_handle->Common.Valid != MPT_VALID) return MPL_FAIL;
	event_handle->Common.Valid = 0;

	r = pthread_mutex_destroy(&event_handle->Handle);
	if (r != 0) goto Error;

	r = pthread_cond_destroy(&event_handle->Cond);
	if (r != 0) goto Error;

	return MPL_SUCCESS;
Error:
	ErrNo_To_Mpl(r);
	return r;
}

int Mpl_Event_Wait(MPL_EVENT_T* event_handle, int rel_milliseconds)
{
	int r = 0;
	struct timespec abstime;

	if (!event_handle) return MPL_FAIL;

	if (!event_handle->IsAuto)
	{
		if (event_handle->IsSet) return MPL_SUCCESS;
	}

	if (rel_milliseconds > 0)
	{
		Mpl_Clock_GetTime(&abstime, rel_milliseconds);
	}
	if ((r = pthread_mutex_lock(&event_handle->Handle)) == 0)
	{
		if (event_handle->IsSet)
		{
			if (event_handle->IsAuto)
			{
				event_handle->IsSet = 0;
			}
			pthread_mutex_unlock(&event_handle->Handle);
			return MPL_SUCCESS;
		}
		if (rel_milliseconds > 0)
		{
			while ((r = pthread_cond_timedwait(&event_handle->Cond, &event_handle->Handle, &abstime)) == 0 && !event_handle->IsSet);
			ErrNo_To_Mpl(r);
		}
		else if (rel_milliseconds < 0)
		{
			while ((r = pthread_cond_wait(&event_handle->Cond, &event_handle->Handle)) == 0 && !event_handle->IsSet);
			ErrNo_To_Mpl(r);
		}
		else
		{
			r = MPL_TIMEOUT;
		}
		if (r == 0 && event_handle->IsAuto && event_handle->IsSet)
		{
			event_handle->IsSet = 0;
		}
		pthread_mutex_unlock(&event_handle->Handle);
	}
	else
	{
		ErrNo_To_Mpl(r);
	}

	return r;
}

int Mpl_Event_Set(MPL_EVENT_T* event_handle)
{
	int r = 0;
	if (!event_handle) return MPL_FAIL;

	if (!event_handle->IsSet)
	{
		if ((r = pthread_mutex_lock(&event_handle->Handle)) != 0)
		{
			ErrNo_To_Mpl(r);
			return r;
		}
		if (!event_handle->IsSet)
		{
			event_handle->IsSet = 1;
			if (event_handle->IsAuto)
			{
				pthread_cond_signal(&event_handle->Cond);
			}
			else
			{
				pthread_cond_broadcast(&event_handle->Cond);
			}
		}
		pthread_mutex_unlock(&event_handle->Handle);
	}
	return MPL_SUCCESS;
}

int Mpl_Event_Reset(MPL_EVENT_T* event_handle)
{
	int r = 0;

	if (!event_handle) return MPL_FAIL;
	if (event_handle->IsSet)
	{
		if ((r = pthread_mutex_lock(&event_handle->Handle)) != 0)
		{
			ErrNo_To_Mpl(r);
			return r;
		}

		if (event_handle->IsSet) event_handle->IsSet = 0;
		pthread_mutex_unlock(&event_handle->Handle);
	}

	return MPL_SUCCESS;
}

int Mpl_Sem_Init(MPL_SEM_T* sem_handle, int sem_value)
{
	int r = 0;
#if MPL_OS_TYPE == MPL_OS_TYPE_OSX
	char semName[24];
	static volatile long m_SemInitCount=0;
	if (!sem_handle || sem_handle->Common.Valid) return MPL_FAIL;
	sprintf(semName,"/Mpl_%08X_%08X",getpid(),MPL_Atomic_Inc32(&m_SemInitCount));
	sem_handle->OsxHandle = sem_open(semName, O_CREAT, 0644, sem_value);
	if (sem_handle->OsxHandle != SEM_FAILED)
	{
		sem_handle->Common.Valid = MPT_VALID;
		return MPL_SUCCESS;
	}
#else
	if (!sem_handle || sem_handle->Common.Valid) return MPL_FAIL;
	if ((r = sem_init(&sem_handle->Handle, 0, sem_value)) != -1)
	{
		sem_handle->Common.Valid = MPT_VALID;
		return MPL_SUCCESS;
	}
#endif
	r = errno;
	ErrNo_To_Mpl(r);
	return r;
}

int Mpl_Sem_Free(MPL_SEM_T* sem_handle)
{
	int r = 0;
	if (!sem_handle || sem_handle->Common.Valid != MPT_VALID) return MPL_FAIL;
	sem_handle->Common.Valid = 0;
#if MPL_OS_TYPE == MPL_OS_TYPE_OSX
	if ((r = sem_close(MPL_Get_Native_Sem(sem_handle))) != -1) return MPL_SUCCESS;
#else
	if ((r = sem_destroy(MPL_Get_Native_Sem(sem_handle))) != -1) return MPL_SUCCESS;
#endif	
	r=errno;
	ErrNo_To_Mpl(r);
	return r;
}

int Mpl_Sem_Wait(MPL_SEM_T* sem_handle)
{
	int r = 0;
	if (!sem_handle || sem_handle->Common.Valid != MPT_VALID) return MPL_FAIL;
	
retry:
	if (sem_wait(MPL_Get_Native_Sem(sem_handle)) == -1)
	{
		if (EINTR == errno) goto retry;
		r=errno;
		ErrNo_To_Mpl(r);
	}
	else
	{
		MPL_Atomic_Dec32(&sem_handle->SemCount);
	}
	return r;
}

int Mpl_Sem_TryWait(MPL_SEM_T* sem_handle)
{
	int r = 0;
	if (!sem_handle || sem_handle->Common.Valid != MPT_VALID) return MPL_FAIL;
	
retry:
	if (sem_trywait(MPL_Get_Native_Sem(sem_handle)) == -1)
	{
		if (EINTR == errno) goto retry;
		r=errno;
		ErrNo_To_Mpl(r);
	}
	else
	{
		MPL_Atomic_Dec32(&sem_handle->SemCount);
	}
	return r;
}

int Mpl_Sem_Release(MPL_SEM_T* sem_handle)
{
	int r = 0;

	if (!sem_handle) return MPL_FAIL;
	if (sem_post(MPL_Get_Native_Sem(sem_handle)) == -1)
	{
		r=errno;
		ErrNo_To_Mpl(r);
	}
	else
	{
		MPL_Atomic_Inc32(&sem_handle->SemCount);
	}
	return r;
}

#elif MPL_THREAD_TYPE == MPL_THREAD_TYPE_WINDOWS

int Mpl_Thread_Init(MPL_THREAD_T* thread_handle, MPL_THREAD_PROC_T* start_fn, void* start_arg)
{
	unsigned int thread_id;

	if (!thread_handle) return MPL_FAIL;

	thread_handle->Handle = (HANDLE)_beginthreadex(NULL, 0, start_fn, start_arg, 0, &thread_id);
	if (thread_handle->Handle && thread_handle->Handle != INVALID_HANDLE_VALUE)
	{
		return MPL_SUCCESS;
	}

	return MPL_FAIL;
}
void Mpl_Thread_End(void* ret_val)
{
	_endthreadex((UINT_PTR)ret_val);
}

int Mpl_Mutex_Init(MPL_MUTEX_T* mutex_handle)
{
	if (!mutex_handle || mutex_handle->Common.Valid) return MPL_FAIL;

	mutex_handle->Handle = CreateMutexA(NULL, FALSE, NULL);
	if (mutex_handle->Handle && mutex_handle->Handle != INVALID_HANDLE_VALUE)
	{
		mutex_handle->Common.Valid = MPT_VALID;
		return MPL_SUCCESS;
	}

	return MPL_FAIL;
}

int Mpl_Mutex_Free(MPL_MUTEX_T* mutex_handle)
{
	if (!mutex_handle || mutex_handle->Common.Valid != MPT_VALID) return MPL_FAIL;
	mutex_handle->Common.Valid = 0;

	if(CloseHandle(mutex_handle->Handle)) return MPL_SUCCESS;
	return MPL_FAIL;
}

int Mpl_Mutex_Wait(MPL_MUTEX_T* mutex_handle)
{
	return Mpl_Event_Wait((MPL_EVENT_T*)mutex_handle, INFINITE);
}

int Mpl_Mutex_TryWait(MPL_MUTEX_T* mutex_handle)
{
	return Mpl_Event_Wait((MPL_EVENT_T*)mutex_handle, 0);
}

int Mpl_Mutex_Release(MPL_MUTEX_T* mutex_handle)
{
	return ReleaseMutex(mutex_handle->Handle) == TRUE ? MPL_SUCCESS : MPL_FAIL;
}

int Mpl_Event_Init(MPL_EVENT_T* event_handle, int is_auto_reset, int initial_state)
{
	if (!event_handle || event_handle->Common.Valid) return MPL_FAIL;

	event_handle->Handle = CreateEventA(NULL, is_auto_reset ? FALSE : TRUE, initial_state ? TRUE : FALSE, NULL);
	if (event_handle->Handle && event_handle->Handle != INVALID_HANDLE_VALUE)
	{
		event_handle->Common.Valid =  MPT_VALID;
		return MPL_SUCCESS;
	}
	return MPL_FAIL;
}

int Mpl_Event_Free(MPL_EVENT_T* event_handle)
{
	return Mpl_Mutex_Free((MPL_MUTEX_T*)event_handle);
}

int Mpl_Event_Wait(MPL_EVENT_T* event_handle, int rel_milliseconds)
{
	DWORD r;
	if (!event_handle || !event_handle->Common.Valid) return MPL_FAIL;

	if (rel_milliseconds < 0) rel_milliseconds = INFINITE;
	r = WaitForSingleObject(event_handle->Handle, rel_milliseconds);
	if (r == WAIT_OBJECT_0) return MPL_SUCCESS;
	if (r == WAIT_FAILED) return MPL_FAIL;

	return MPL_ABANDONED;
}

int Mpl_Event_Set(MPL_EVENT_T* event_handle)
{
	if (!event_handle || !event_handle->Common.Valid) return MPL_FAIL;
	event_handle->IsSet = 1;
	return SetEvent(event_handle->Handle) == TRUE ? MPL_SUCCESS : MPL_FAIL;
}

int Mpl_Event_Reset(MPL_EVENT_T* event_handle)
{
	if (!event_handle) return MPL_FAIL;

	event_handle->IsSet = 0;
	return ResetEvent(event_handle->Handle) == TRUE ? MPL_SUCCESS : MPL_FAIL;
}

int Mpl_Sem_Init(MPL_SEM_T* sem_handle, int sem_value)
{
	if (!sem_handle || sem_handle->Common.Valid) return MPL_FAIL;

	sem_handle->Handle = CreateSemaphoreA(NULL, sem_value, INT_MAX, NULL);
	if (sem_handle->Handle && sem_handle->Handle != INVALID_HANDLE_VALUE)
	{
		sem_handle->Common.Valid = MPT_VALID;
		return MPL_SUCCESS;
	}
	return MPL_FAIL;
}
int Mpl_Sem_Free(MPL_SEM_T* sem_handle)
{
	return Mpl_Mutex_Free((MPL_MUTEX_T*)sem_handle);
}

int Mpl_Sem_Wait(MPL_SEM_T* sem_handle)
{
	int r;
	if (!sem_handle || sem_handle->Common.Valid != MPT_VALID) return MPL_FAIL;
	if ((r=Mpl_Event_Wait((MPL_EVENT_T*)sem_handle, INFINITE)) == MPL_SUCCESS)
	{
		MPL_Atomic_Dec32(&sem_handle->SemCount);
		return MPL_SUCCESS;
	}
	return r;
}

int Mpl_Sem_TryWait(MPL_SEM_T* sem_handle)
{
	int r;
	if (!sem_handle || sem_handle->Common.Valid != MPT_VALID) return MPL_FAIL;
	if ((r=Mpl_Event_Wait((MPL_EVENT_T*)sem_handle, 0)) == MPL_SUCCESS)
	{
		MPL_Atomic_Dec32(&sem_handle->SemCount);
		return MPL_SUCCESS;
	}
	return r;
}

int Mpl_Sem_Release(MPL_SEM_T* sem_handle)
{
	if (!sem_handle || sem_handle->Common.Valid != MPT_VALID) return MPL_FAIL;

	if (ReleaseSemaphore(sem_handle->Handle, 1, NULL))
	{
		MPL_Atomic_Inc32(&sem_handle->SemCount);
		return MPL_SUCCESS;
	}

	return MPL_FAIL;
}

#else
#error "Thread functions not implemented for this platform/OS."
#endif

int Mpl_Sem_GetCount(MPL_SEM_T* sem_handle, volatile long* sem_value)
{
	if (!sem_handle || sem_handle->Common.Valid != MPT_VALID) return MPL_FAIL;
	*sem_value = sem_handle->SemCount;
	return MPL_SUCCESS;
}

int Mpl_Init(void)
{
	if (MPL_Atomic_Inc32(&g_MplInitLock) == 1)	
	{
#   if MPL_OS_TYPE == MPL_OS_TYPE_OSX
		// OS X does not have clock_gettime, use clock_get_time
		host_name_port_t host_self;
		host_self = mach_host_self();
		if (host_get_clock_service(host_self, REALTIME_CLOCK, &g_ClkId_RelativeTime) != 0)
			host_get_clock_service(host_self, CALENDAR_CLOCK, &g_ClkId_RelativeTime);
		host_get_clock_service(host_self, CALENDAR_CLOCK, &g_ClkId_AbsoluteTime);
		mach_port_deallocate(mach_task_self(), host_self);
#    elif MPL_OS_TYPE == MPL_OS_TYPE_WINDOWS
		LARGE_INTEGER perfFreq;
		QueryPerformanceFrequency(&perfFreq);
		g_ClockFreqPerSec = (double)perfFreq.QuadPart;
		g_ClockFreqPerMs = g_ClockFreqPerSec / 1000.0;
		g_ClockFreqPerUs = g_ClockFreqPerMs / 1000.0;
		g_ClockFreqPerNs = g_ClockFreqPerUs / 1000.0;
#    elif MPL_OS_TYPE == MPL_OS_TYPE_LINUX
		struct timespec ts_dummy;
		g_ClkId_RelativeTime = CLOCK_REALTIME;
		g_ClkId_AbsoluteTime = CLOCK_REALTIME;
		if (clock_gettime(CLOCK_MONOTONIC, &ts_dummy) == 0)
			g_ClkId_RelativeTime = CLOCK_MONOTONIC;
#    endif

		return 0;
	}

	return 0;
}

void Mpl_Free(void)
{
	if (MPL_Atomic_Dec32(&g_MplInitLock) == 0)	
	{
#   if MPL_OS_TYPE == MPL_OS_TYPE_OSX
		mach_port_deallocate(mach_task_self(), g_ClkId_AbsoluteTime);
		mach_port_deallocate(mach_task_self(), g_ClkId_RelativeTime);
#   endif
	}
}

#if MPL_OS_TYPE == MPL_OS_TYPE_WINDOWS
/* Workaround for testing on windows via libusbX.*/
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 1
#endif
/*
 * time between jan 1, 1601 and jan 1, 1970 in units of 100 nanoseconds
 */
#ifndef PTW32_TIMESPEC_TO_FILETIME_OFFSET
#define PTW32_TIMESPEC_TO_FILETIME_OFFSET ( ((LONGLONG) 27111902 << 32) + (LONGLONG) 3577643008 )
#endif

static void filetime_to_timespec (const FILETIME* ft, struct timespec* ts)
/*
 * -------------------------------------------------------------------
 * converts FILETIME (as set by GetSystemTimeAsFileTime), where the time is
 * expressed in 100 nanoseconds from Jan 1, 1601,
 * into struct timespec
 * where the time is expressed in seconds and nanoseconds from Jan 1, 1970.
 * -------------------------------------------------------------------
 */
{
	ts->tv_sec = (int) ((*(LONGLONG*) ft - PTW32_TIMESPEC_TO_FILETIME_OFFSET) / 10000000);
	ts->tv_nsec = (int) ((*(LONGLONG*) ft - PTW32_TIMESPEC_TO_FILETIME_OFFSET - ((LONGLONG) ts->tv_sec * (LONGLONG) 10000000)) * 100);
}
#endif

void Mpl_Clock_GetTime(struct timespec* abstime, int ms_add_delta)
{
	/* OSX and Windows do not have a clock_gettime api. */
#if MPL_OS_TYPE == MPL_OS_TYPE_OSX
	mach_timespec_t mts;
	clock_get_time(g_ClkId_AbsoluteTime, &mts);
	abstime->tv_sec = mts.tv_sec;
	abstime->tv_nsec = mts.tv_nsec;
#elif MPL_OS_TYPE == MPL_OS_TYPE_WINDOWS
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	filetime_to_timespec(&ft, abstime);
#elif MPL_OS_TYPE == MPL_OS_TYPE_LINUX
	clock_gettime(CLOCK_REALTIME, abstime);
#else
#error "Clock functions not implemented for this platform/OS."
#endif

	/* common section */
	if (ms_add_delta)
	{
		Mpl_Clock_AddMs(abstime, ms_add_delta);
	}
}
void Mpl_Clock_AddMs(struct timespec* abstime, int ms_delta)
{
	// nothing to do
	if ((ms_delta)==0) return;

	abstime->tv_sec += (ms_delta / 1000);
	abstime->tv_nsec += (((mint64_t)(ms_delta) % 1000) * 1000000);
	if (abstime->tv_nsec < 0) {
		abstime->tv_sec -= 1;
		abstime->tv_nsec+= 1000000000;
	} else if (abstime->tv_nsec >= 1000000000) {
		abstime->tv_sec += 1;
		abstime->tv_nsec-= 1000000000;
	}
}

double Mpl_Clock_Ticks(void)
{
	double tickTime;
#if MPL_OS_TYPE == MPL_OS_TYPE_WINDOWS
	LARGE_INTEGER clockTicks;
	QueryPerformanceCounter(&clockTicks);
	tickTime = ((double)clockTicks.QuadPart / g_ClockFreqPerSec);
#else
	struct timespec abstime;
#	if MPL_OS_TYPE == MPL_OS_TYPE_OSX
	mach_timespec_t mts;
	clock_get_time(g_ClkId_RelativeTime, &mts);
	abstime.tv_sec = mts.tv_sec;
	abstime.tv_nsec = mts.tv_nsec;
#	elif MPL_OS_TYPE == MPL_OS_TYPE_LINUX
	clock_gettime(CLOCK_REALTIME, &abstime);
#	else
#	error "Clock functions not implemented for this platform/OS."
#	endif

	tickTime = abstime.tv_nsec;
	tickTime /= 1000000.0;	// ns to ms
	tickTime /= 1000.0;		// ms to sec

	tickTime += abstime.tv_sec;
#endif

	return tickTime;
}

muint64_t Mpl_Clock_Ticks_Ms(void)
{
	muint64_t tickTime;
#if MPL_OS_TYPE == MPL_OS_TYPE_WINDOWS
	LARGE_INTEGER clockTicks;
	QueryPerformanceCounter(&clockTicks);

	tickTime = (muint64_t)((double)clockTicks.QuadPart / g_ClockFreqPerMs);
#else
	struct timespec abstime;
#	if MPL_OS_TYPE == MPL_OS_TYPE_OSX
	mach_timespec_t mts;
	clock_get_time(g_ClkId_RelativeTime, &mts);
	abstime.tv_sec = mts.tv_sec;
	abstime.tv_nsec = mts.tv_nsec;
#	elif MPL_OS_TYPE == MPL_OS_TYPE_LINUX
	clock_gettime(g_ClkId_RelativeTime, &abstime);
#	else
#	error "Clock functions not implemented for this platform/OS."
#	endif

	tickTime = abstime.tv_sec * 1000;		// sec to ms
	tickTime += abstime.tv_nsec / 1000000;	// ns to ms
#endif

	return tickTime;
}

muint64_t Mpl_Clock_Ticks_Us(void)
{
	muint64_t tickTime;
#if MPL_OS_TYPE == MPL_OS_TYPE_WINDOWS
	LARGE_INTEGER clockTicks;
	QueryPerformanceCounter(&clockTicks);

	tickTime = (muint64_t)((double)clockTicks.QuadPart / g_ClockFreqPerUs);
#else
	struct timespec abstime;
#	if MPL_OS_TYPE == MPL_OS_TYPE_OSX
	mach_timespec_t mts;
	clock_get_time(g_ClkId_RelativeTime, &mts);
	abstime.tv_sec = mts.tv_sec;
	abstime.tv_nsec = mts.tv_nsec;
#	elif MPL_OS_TYPE == MPL_OS_TYPE_LINUX
	clock_gettime(g_ClkId_RelativeTime, &abstime);
#	else
#	error "Clock functions not implemented for this platform/OS."
#	endif

	tickTime = abstime.tv_sec * 1000000;	// sec to us
	tickTime += abstime.tv_nsec / 1000;		// ns to us
#endif

	return tickTime;
}

muint64_t Mpl_Clock_Ticks_Ns(void)
{
	muint64_t tickTime;
#if MPL_OS_TYPE == MPL_OS_TYPE_WINDOWS
	LARGE_INTEGER clockTicks;
	QueryPerformanceCounter(&clockTicks);

	tickTime = (muint64_t)((double)clockTicks.QuadPart / g_ClockFreqPerNs);
#else
	struct timespec abstime;
#	if MPL_OS_TYPE == MPL_OS_TYPE_OSX
	mach_timespec_t mts;
	clock_get_time(g_ClkId_RelativeTime, &mts);
	abstime.tv_sec = mts.tv_sec;
	abstime.tv_nsec = mts.tv_nsec;
#	elif MPL_OS_TYPE == MPL_OS_TYPE_LINUX
	clock_gettime(g_ClkId_RelativeTime, &abstime);
#	else
#	error "Clock functions not implemented for this platform/OS."
#	endif

	tickTime = abstime.tv_sec * 1000;	// sec to ms
	tickTime *= 1000000;				// ms to ns
	tickTime += abstime.tv_nsec;
#endif

	return tickTime;
}
