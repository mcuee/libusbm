#ifndef __MPL_THREADS_H__
#define __MPL_THREADS_H__

#define MPL_FORCE_PTHREADS 1

#include <stdlib.h>
#include <limits.h>
#include <errno.h>

#define MPL_SUCCESS		(0)
#define MPL_TIMEOUT		(ETIMEDOUT)
#define MPL_FAIL		(EINVAL)
#define MPL_ABANDONED	(EPERM)

#define MPT_VALID ((int)0x1AB4)

#ifdef MPL_OS_TYPE
#undef MPL_OS_TYPE
#endif
#define MPL_OS_TYPE_OSX		(1)
#define MPL_OS_TYPE_WINDOWS (2)
#define MPL_OS_TYPE_LINUX	(3)

#ifdef MPL_THREAD_TYPE
#undef MPL_THREAD_TYPE
#endif
#define MPL_THREAD_TYPE_PTHREADS	(1)
#define MPL_THREAD_TYPE_WINDOWS		(2)

#if defined(__MACH__)		/* OSX */
#  define MPL_OS_TYPE MPL_OS_TYPE_OSX
#  ifdef MPL_FORCE_PTHREADS
#    undef MPL_FORCE_PTHREADS
#  endif
#  define MPL_FORCE_PTHREADS 1
#  include <stdio.h>
#  include <unistd.h>
#  include <stdlib.h>
#  include <stdint.h>
#  include <libkern/OSAtomic.h>
#  include <mach/clock.h>
#  include <mach/mach.h>
#  ifndef mint64_t
#    define mint64_t int64_t
#  endif
#  ifndef muint64_t
#    define muint64_t uint64_t
#  endif
#  ifndef INFINITE
#    define INFINITE (0xFFFFFFFF)
#  endif
#  define MPL_SleepMs(mValue) usleep((mValue)*1000)

#elif defined(_WIN32)
/* WINDOWS */
#  define MPL_OS_TYPE MPL_OS_TYPE_WINDOWS
#  include <windows.h>
#  include <stdio.h>
#  include <stdlib.h>
#  include <errno.h>
#  ifndef mint64_t
#    define mint64_t __int64
#  endif
#  ifndef muint64_t
#    define muint64_t unsigned __int64
#  endif
#  define MPL_SleepMs(mValue) Sleep(mValue)

#  ifndef HAVE_STRUCT_TIMESPEC
#  define HAVE_STRUCT_TIMESPEC 1
struct timespec
{
	long tv_sec;
	long tv_nsec;
};
#  endif /* HAVE_STRUCT_TIMESPEC */

#else
/* LINUX */
#  define MPL_OS_TYPE MPL_OS_TYPE_LINUX
#  ifdef MPL_FORCE_PTHREADS
#    undef MPL_FORCE_PTHREADS
#  endif
#  define MPL_FORCE_PTHREADS 1
#  include <stdio.h>
#  include <unistd.h>
#  include <stdlib.h>
#  include <stdint.h>
#  include <string.h>

#  ifndef mint64_t
#    define mint64_t int64_t
#  endif
#  ifndef muint64_t
#    define muint64_t uint64_t
#  endif
#  ifndef INFINITE
#    define INFINITE (0xFFFFFFFF)
#  endif
#  ifndef TRUE
#    define TRUE  (1)
#    define FALSE (0)
#  endif
#  define MPL_SleepMs(mValue) usleep((mValue)*1000)
#endif

/* Atomic ops macros:
MPL_Atomic_Add32
MPL_Atomic_Inc32
MPL_Atomic_Dec32
MPL_Atomic_CmpExg		(OSX,WIN, or NEW GCC ONLY)
MPL_Atomic_CmpExg32		(OSX,WIN, or NEW GCC ONLY)
MPL_Atomic_CmpExgPtr	(OSX,WIN, or NEW GCC ONLY)
*/

#if  defined(__GNUC__) && (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__) > 40100
#  define MPL_Atomic_Add32(mValuePtr, mAddValue) __sync_add_and_fetch(mValuePtr,mAddValue)
#  define MPL_Atomic_Inc32(mValuePtr) MPL_Atomic_Add32(mValuePtr,1)
#  define MPL_Atomic_Dec32(mValuePtr) MPL_Atomic_Add32(mValuePtr,-1)
#  define MPL_Atomic_CmpExg(mTheValue,mNewValue,mCmpValue) (__sync_bool_compare_and_swap(mTheValue, mCmpValue, mNewValue) ? 1 : 0)
#  define MPL_Atomic_CmpExg32(mTheValue,mNewValue,mCmpValue) MPL_Atomic_CmpExg(mTheValue,mNewValue,mCmpValue)
#  define MPL_Atomic_CmpExgPtr(mTheValue,mNewValue,mCmpValue) MPL_Atomic_CmpExg(mTheValue,mNewValue,mCmpValue)
#elif MPL_OS_TYPE == MPL_OS_TYPE_WINDOWS
#  define MPL_Atomic_Add32(mValuePtr, mAddValue) InterlockedAdd(mValuePtr, mAddValue)
#  define MPL_Atomic_Inc32(mValuePtr) InterlockedIncrement(mValuePtr)
#  define MPL_Atomic_Dec32(mValuePtr) InterlockedDecrement(mValuePtr)
#  define MPL_Atomic_CmpExg32(mTheValue,mNewValue,mCmpValue) ((InterlockedCompareExchange(mTheValue, mNewValue, mCmpValue) == (mNewValue)) ? 1 : 0)
#  define MPL_Atomic_CmpExgPtr(mTheValue,mNewValue,mCmpValue) ((InterlockedCompareExchangePtr(mTheValue, mNewValue, mCmpValue) == (mNewValue)) ? 1 : 0)
#elif  MPL_OS_TYPE == MPL_OS_TYPE_OSX
#  define MPL_Atomic_Add32(mValuePtr, mAddValue) OSAtomicAdd32(mAddValue,mValuePtr)
#  define MPL_Atomic_Inc32(mValuePtr) OSAtomicIncrement32(mValuePtr)
#  define MPL_Atomic_Dec32(mValuePtr) OSAtomicDecrement32(mValuePtr)
#  define MPL_Atomic_CmpExg32(mTheValue,mNewValue,mCmpValue) OSAtomicCompareAndSwap32(mCmpValue, mNewValue, mTheValue)
#  if !defined(__LP64__)
#    define MPL_Atomic_CmpExgPtr(mTheValue,mNewValue,mCmpValue) OSAtomicCompareAndSwap32((int32_t)(void*) mCmpValue, (int32_t)(void*) mNewValue, (int32_t*)(void**)mTheValue)
#  else
#    define MPL_Atomic_CmpExgPtr(mTheValue,mNewValue,mCmpValue) OSAtomicCompareAndSwap64((int64_t)(void*) mCmpValue, (int64_t)(void*) mNewValue, (int64_t*)(void**)mTheValue)
#  endif
#elif  MPL_OS_TYPE == MPL_OS_TYPE_LINUX && defined(__unix__) || defined(__linux__)
#  include <sys/atomic.h>
#  define MPL_Atomic_Add32(mValuePtr, mAddValue) atomic_add_32_nv((uint32_t*)mValuePtr,mAddValue)
#  define MPL_Atomic_Inc32(mValuePtr) atomic_inc_32_nv(mValuePtr)
#  define MPL_Atomic_Dec32(mValuePtr) atomic_dec_32_nv(mValuePtr)
#  define MPL_Atomic_CmpExg32(mTheValue,mNewValue,mCmpValue) error "Use a new GCC compiler to support this operation."
#  define MPL_Atomic_CmpExgPtr(mTheValue,mNewValue,mCmpValue) error "Use a new GCC compiler to support this operation."
#else
#    error "Atomic functions not implemented for this platform/OS. Please report this to the libusb-win32-devel mailing list."
#endif

#ifndef MPL_FORCE_PTHREADS
#  define MPL_FORCE_PTHREADS 0
#endif

#if (MPL_FORCE_PTHREADS==1)
#  define MPL_THREAD_TYPE MPL_THREAD_TYPE_PTHREADS
#  define MPL_THDPROC_CC
#  define MPL_THDPROC_RETURN_TYPE void*
#elif defined(_WIN32)
#  define MPL_THREAD_TYPE MPL_THREAD_TYPE_WINDOWS
#  define MPL_THDPROC_CC __stdcall
#  define MPL_THDPROC_RETURN_TYPE unsigned
#else
#  error "Thread functions not implemented for this platform/OS. Please report this to the libusb-win32-devel mailing list."
#endif

struct _MPL_COMMON_T
{
	int Valid;
	muint64_t UserContext;
	muint64_t zReserved;
};
typedef struct _MPL_COMMON_T MPL_COMMON_T,*PMPL_COMMON_T;

#if MPL_THREAD_TYPE == MPL_THREAD_TYPE_WINDOWS
#  include <process.h>
#  define MPL_Get_Native_Sem(mMplSemHandle) ((mMplSemHandle)->Handle)

struct _MPL_THREAD_T
{
	MPL_COMMON_T Common;
	HANDLE Handle;
};
struct _MPL_MUTEX_T
{
	MPL_COMMON_T Common;
	HANDLE Handle;
};
struct _MPL_EVENT_T
{
	MPL_COMMON_T Common;
	HANDLE Handle;
	int IsAuto;
	volatile long IsSet;
};
struct _MPL_SEM_T
{
	MPL_COMMON_T Common;
	HANDLE Handle;
	volatile long SemCount;
};
#elif MPL_THREAD_TYPE == MPL_THREAD_TYPE_PTHREADS
#  include <pthread.h>
#  include <semaphore.h>
#  if MPL_OS_TYPE == MPL_OS_TYPE_OSX
#    define MPL_Get_Native_Sem(mMplSemHandle) ((mMplSemHandle)->OsxHandle)
#  else
#    define MPL_Get_Native_Sem(mMplSemHandle) (&((mMplSemHandle)->Handle))
#  endif
struct _MPL_THREAD_T
{
	MPL_COMMON_T Common;
	pthread_t Handle;
};
struct _MPL_MUTEX_T
{
	MPL_COMMON_T Common;
	pthread_mutex_t Handle;
};
struct _MPL_EVENT_T
{
	MPL_COMMON_T Common;
	pthread_mutex_t Handle;
	int IsAuto;

	volatile long IsSet;
	pthread_cond_t Cond;
};
struct _MPL_SEM_T
{
	MPL_COMMON_T Common;
	volatile long SemCount;
	sem_t Handle;
	sem_t* OsxHandle;
};
#else
#	error "Thread functions not implemented for this platform/OS. Please report this to the libusb-win32-devel mailing list."
#endif

#ifndef ETIMEDOUT
#define ETIMEDOUT (116)
#endif
#ifndef EOVERFLOW
#define EOVERFLOW	(132)
#endif
#ifndef ENODATA
#define ENODATA		(206)
#endif

typedef MPL_THDPROC_RETURN_TYPE MPL_THDPROC_CC MPL_THREAD_PROC_T(void*);

typedef struct _MPL_THREAD_T	MPL_THREAD_T;
typedef struct _MPL_EVENT_T		MPL_EVENT_T;
typedef struct _MPL_MUTEX_T		MPL_MUTEX_T;
typedef struct _MPL_SEM_T		MPL_SEM_T;

int Mpl_Init(void);
void Mpl_Free(void);

int Mpl_Thread_Init(MPL_THREAD_T* thread_handle, MPL_THREAD_PROC_T* start_fn, void* start_arg);
void Mpl_Thread_End(void* ret_val);

int Mpl_Mutex_Init(MPL_MUTEX_T* mutex_handle);
int Mpl_Mutex_Free(MPL_MUTEX_T* mutex_handle);
int Mpl_Mutex_Wait(MPL_MUTEX_T* mutex_handle);
int Mpl_Mutex_TryWait(MPL_MUTEX_T* mutex_handle);
int Mpl_Mutex_Release(MPL_MUTEX_T* mutex_handle);

int Mpl_Event_Init(MPL_EVENT_T* event_handle, int is_auto_reset, int initial_state);
int Mpl_Event_Free(MPL_EVENT_T* event_handle);
int Mpl_Event_Wait(MPL_EVENT_T* event_handle, int rel_milliseconds);
int Mpl_Event_Set(MPL_EVENT_T* event_handle);
int Mpl_Event_Reset(MPL_EVENT_T* event_handle);

int Mpl_Sem_Init(MPL_SEM_T* sem_handle, int sem_value);
int Mpl_Sem_Free(MPL_SEM_T* sem_handle);
int Mpl_Sem_Wait(MPL_SEM_T* sem_handle);
int Mpl_Sem_TryWait(MPL_SEM_T* sem_handle);
int Mpl_Sem_Release(MPL_SEM_T* sem_handle);
int Mpl_Sem_GetCount(MPL_SEM_T* sem_handle, volatile long* sem_value);

void Mpl_Clock_GetTime(struct timespec* abstime, int ms_add_delta);
void Mpl_Clock_AddMs(struct timespec* abstime, int ms_delta);

double Mpl_Clock_Ticks(void);
muint64_t Mpl_Clock_Ticks_Ms(void);
muint64_t Mpl_Clock_Ticks_Us(void);
muint64_t Mpl_Clock_Ticks_Ns(void);

#endif
