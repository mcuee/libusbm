/* Mpl_Test

 Copyright (C) 2012 Travis Robinson. <libusbdotnet@gmail.com>
 http://sourceforge.net/projects/libusb-win32

 This program is free software; you can redistribute it and/or modify it
 under the terms of the GNU Lesser General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful, but
 WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 License for more details.

 You should have received a copy of the GNU Lesser General Public License
 along with this program; if not, please visit www.gnu.org.
*/

#include "mpl_threads.h"

// All output is directed through these macros.
#define CONLOG(...)  printf(__VA_ARGS__)
#define CONDBG(...) CONLOG(__VA_ARGS__)
#define CONMSG(...) CONLOG(__VA_ARGS__)
#define CONWRN(...) CONLOG("Warn!  " __VA_ARGS__)
#define CONERR(...) CONLOG("Error! " __VA_ARGS__)

struct _MPT_ARG_CONTAINER
{
	int _no_cmd_args_yet;
};
typedef struct _MPT_ARG_CONTAINER MPT_ARG_CONTAINER, *PMPT_ARG_CONTAINER;

#if MPL_OS_TYPE == MPL_OS_TYPE_WINDOWS
#  include <conio.h>
#  define PRINTF_I64 "%I64d"
#  define MPL_SleepMs(mValue) Sleep(mValue)
#  define MAIN_CC _cdecl

#  define ConIO_IsKeyAvailable() _kbhit()
#  define ConIO_EchoInput_Disabled()
#  define ConIO_EchoInput_Enabled()
#  define ConIO_GetCh() _getch()

#elif MPL_OS_TYPE == MPL_OS_TYPE_OSX || MPL_OS_TYPE == MPL_OS_TYPE_LINUX
#  include <termios.h>
#  include <stdio.h>
#  include <fcntl.h>
#  define PRINTF_I64 "%lld"
#  define MPL_SleepMs(mValue) usleep((mValue)*1000)
#  define MAIN_CC

#  define ConIO_EchoInput_Disabled() conio_changemode(1)
#  define ConIO_EchoInput_Enabled() conio_changemode(0)
#  define ConIO_IsKeyAvailable() conio_kbhit()
#  define ConIO_GetCh() getchar()

void conio_changemode(int dir)
{
	static struct termios oldt, newt;

	if ( dir == 1 )
	{
		tcgetattr( STDIN_FILENO, &oldt);
		newt = oldt;
		newt.c_lflag &= ~( ICANON | ECHO );
		tcsetattr( STDIN_FILENO, TCSANOW, &newt);
	}
	else
		tcsetattr( STDIN_FILENO, TCSANOW, &oldt);
}

int conio_kbhit(void)
{
	struct timeval tv;
	fd_set rdfs;

	tv.tv_sec = 0;
	tv.tv_usec = 0;

	FD_ZERO(&rdfs);
	FD_SET (STDIN_FILENO, &rdfs);

	select(STDIN_FILENO + 1, &rdfs, NULL, NULL, &tv);
	return FD_ISSET(STDIN_FILENO, &rdfs);
}
#else
#  error "Benchmark application not supported on for this platform."
#endif

/* Globals: */
struct _MPT_GLOBALS
{
	MPL_THREAD_T g_Thread;
	MPL_EVENT_T g_ThreadEvent_Running;
	MPL_EVENT_T g_ThreadEvent_Terminated;
	MPL_SEM_T semHandle;
};
typedef struct _MPT_GLOBALS MPT_GLOBALS,*PMPT_GLOBALS;

MPT_GLOBALS g_Mpt;

static char* Str_ToLower(char* s);
static int   Parse_Args(PMPT_ARG_CONTAINER argContainer, int argc, char** argv);
static char* Parse_StrVal(const char* src, const char* paramName);
static int   Parse_IntVal(const char* src, const char* paramName, int* returnValue);
static void  Show_Help(void);
static void  Show_Copyright(void);

static MPL_THDPROC_RETURN_TYPE MPL_THDPROC_CC Test_Thread_Proc1(void* user_context);

static char* Str_ToLower(char* s)
{
	char* p = s;
	if (!s || !*s) return s;
	do
	{
		if (*p >= 'A' && *p <= 'Z') *p += 32;
	}
	while(*(++p) != '\0');
	return s;
}

static char* Parse_StrVal(const char* src, const char* paramName)
{
	return (strstr(src, paramName) == src) ? (char*)(src + strlen(paramName)) : NULL;
}

static int Parse_IntVal(const char* src, const char* paramName, int* returnValue)
{
	char* value = Parse_StrVal(src, paramName);
	if (value)
	{
		*returnValue = strtol(value, NULL, 0);
		return TRUE;
	}
	return FALSE;
}

static int Parse_Args(PMPT_ARG_CONTAINER argContainer, int argc, char** argv)
{
	char arg[128];
	char* value;
	int iarg;
	int r = EINVAL;

	for (iarg = 1; iarg < argc; iarg++)
	{
		if (strlen(argv[iarg]) >= sizeof(arg))
			return -1;
		else
			strcpy(arg, argv[iarg]);

		Str_ToLower(arg);

		/* TODO: */

	}
	return r;
}

int Report_Result(int passed)
{
	if (passed)
	{
		CONMSG(" Passed!\n");
	}
	else
	{
		CONMSG(" Failed!\n");
	}

	return passed;
}

static MPL_THDPROC_RETURN_TYPE MPL_THDPROC_CC Test_Thread_Proc1(void* user_context)
{
	int ec = 0;
	void* r = user_context;
	double clockTime;
	MPT_GLOBALS* theadContext = (MPT_GLOBALS*)user_context;
	MPL_SleepMs(0);
	CONMSG("    Thread Starting.. \n");
	if ((ec=Mpl_Event_Wait(&theadContext->g_ThreadEvent_Running, INFINITE)) != MPL_SUCCESS)
	{
		CONERR("    Mpl_Event_Wait failed. ret=%d\n",ec);
		r = NULL;
		goto Done;
	}
	CONMSG("    Thread Resuming.. \n");

	clockTime = Mpl_Clock_Ticks();
	CONMSG("    clock time from thread: %f (secs)\n", clockTime);

Done:
	CONMSG("    Thread Terminating.. \n");
	if ((ec = Mpl_Event_Set(&theadContext->g_ThreadEvent_Terminated)) != MPL_SUCCESS)
	{
		CONERR("    Mpl_Event_Set failed. ret=%d\n", ec);
		r = NULL;
	}
	return (MPL_THDPROC_RETURN_TYPE)r;
}

int MAIN_CC main(int argc, char** argv)
{
	int r=0;
	int i;
	volatile long atom_inc32_test = 0;
	int passed;
	const char* test_name;
	struct timespec clockTime;
	double clockTimeSec;

	memset(&g_Mpt,0,sizeof(g_Mpt));
	Mpl_Init();
	ConIO_EchoInput_Disabled();

   
	CONMSG("Checking Semaphore Support..");
	if ((r=Mpl_Sem_Init(&g_Mpt.semHandle, 1)) != MPL_SUCCESS)
	{
		CONERR(" Failed!\n Mpl_Sem_Init failed. r=%d errno=%d\n",r,errno);
	} else {
		volatile long sem_value=0;
		CONMSG(" Passed!\n");
        
        do
        {
            if ((r = Mpl_Sem_TryWait(&g_Mpt.semHandle)) != MPL_SUCCESS) {
                CONERR(" Mpl_Sem_TryWait failed. r=%d errno=%d\n",r,errno);
            } else  {
                CONMSG("  try-wait = %d\n",++sem_value);               
            }
        
        }while(r == MPL_SUCCESS);
		if ((r = Mpl_Sem_Release(&g_Mpt.semHandle)) != MPL_SUCCESS) {
			CONERR(" Mpl_Sem_Release failed. r=%d errno=%d\n",r,errno);
		}

        
		if ((r = Mpl_Sem_GetCount(&g_Mpt.semHandle, &sem_value)) != MPL_SUCCESS) {
			CONERR(" Mpl_Sem_GetCount failed. r=%d errno=%d\n",r,errno);
		} else {
			CONMSG("  - sem-count = %d\n",sem_value);
		}
        
		if ((r = Mpl_Sem_Free(&g_Mpt.semHandle)) != MPL_SUCCESS) {
			CONERR(" Mpl_Sem_Free failed. r=%d errno=%d\n",r,errno);
		}
	}

	test_name = "Atomic Inc/Dec 32:";
	CONMSG("%s",test_name);
	passed = ((MPL_Atomic_Inc32(&atom_inc32_test) == 1) && (MPL_Atomic_Dec32(&atom_inc32_test) == 0)) ? 1 : 0;
	if (!Report_Result(passed)) goto Done;

	test_name = "[Mpl_Clock_GetTime]";
	CONMSG("%s\n",test_name);
	for(i=0; i < 10; i++)
	{
		Mpl_Clock_GetTime(&clockTime, 0);
		CONMSG("  - add-ms = %d\n",0);
		CONMSG("    secs   = %d\n",clockTime.tv_sec);
		CONMSG("    nsecs  = %d\n",clockTime.tv_nsec);

		Mpl_Clock_GetTime(&clockTime, 1000);
		CONMSG("  - add-ms = %d\n",1000);
		CONMSG("    secs   = %d\n",clockTime.tv_sec);
		CONMSG("    nsecs  = %d\n",clockTime.tv_nsec);

		Mpl_Clock_GetTime(&clockTime, -1000);
		CONMSG("  - add-ms = %d\n",-1000);
		CONMSG("    secs   = %d\n",clockTime.tv_sec);
		CONMSG("    nsecs  = %d\n\n",clockTime.tv_nsec);
		MPL_SleepMs(1000);
	}
	test_name = "[Mpl_Clock_Ticks]";
	CONMSG("%s\n",test_name);
	clockTimeSec = Mpl_Clock_Ticks();
	CONMSG("  ticks  = %f (secs)\n",clockTimeSec);

	CONMSG("Creating Running Event:");
	if ((r=Mpl_Event_Init(&g_Mpt.g_ThreadEvent_Running, 1, 0)) != MPL_SUCCESS)
	{
		CONERR(" Failed!\n  Mpl_Event_Init failed. ret=%d\n",r);
		goto Done;
	} else {
		CONMSG(" Passed!\n");
	}

	CONMSG("Creating Terminated Event:");
	if ((r=Mpl_Event_Init(&g_Mpt.g_ThreadEvent_Terminated, 0, 0)) != MPL_SUCCESS)
	{
		CONERR(" Failed!\n  Mpl_Event_Init failed. ret=%d\n",r);
		goto Done;
	} else {
		CONMSG(" Passed!\n");
	}

	CONMSG("Starting Thread:");
	if ((r = Mpl_Thread_Init(&g_Mpt.g_Thread, Test_Thread_Proc1, &g_Mpt)) != MPL_SUCCESS)
	{
		CONERR(" Failed!\n  Mpl_Thread_Init failed. ret=%d\n",r);
		goto Done;
	} else {
		CONMSG(" Passed!\n");
	}

	CONMSG("Resuming Thread:");
	if ((r = Mpl_Event_Set(&g_Mpt.g_ThreadEvent_Running)) != MPL_SUCCESS)
	{
		CONERR(" Failed!\n  Mpl_Event_Set failed. ret=%d\n",r);
		goto Done;
	} else {
		CONMSG("\n  Waiting for Thread Terminate..\n");
		if ((r = Mpl_Event_Wait(&g_Mpt.g_ThreadEvent_Terminated, INFINITE)) != MPL_SUCCESS)
		{
			CONERR("   Failed!\n  Mpl_Event_Wait failed. ret=%d\n",r);
			goto Done;
		} else {
			CONMSG("Passed!\n");
		}
	}

Done:
	ConIO_EchoInput_Enabled();
	Mpl_Event_Free(&g_Mpt.g_ThreadEvent_Running);
	Mpl_Event_Free(&g_Mpt.g_ThreadEvent_Terminated);
	Mpl_Free();
	return passed;
}

static void Show_Help(void)
{
	CONMSG("TODO:\n");

}

static void Show_Copyright(void)
{
	CONMSG("Mpl_Test\n");
	CONMSG("Copyright (c) 2012 Travis Robinson. <libusbdotnet@gmail.com>\n");
	CONMSG("http://sourceforge.net/projects/libusb-win32\n");
}
