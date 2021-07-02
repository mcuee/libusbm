/* USB Benchmark for libusb-win32

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

#define MAX_OUTSTANDING_TRANSFERS 10

// All output is directed through these macros.
#define LOG(...)  printf(__VA_ARGS__)

// This is used only in VerifyData() for display information
// about data validation mismatches.
#define CONVDAT(...) LOG("[data-mismatch] " __VA_ARGS__)

#define CONERR(...) LOG("Err: " __VA_ARGS__)
#define CONMSG(...) LOG(__VA_ARGS__)
#define CONWRN(...) LOG("Wrn: " __VA_ARGS__)
#define CONDBG(...) LOG(__VA_ARGS__)

#define XFERLOG(mLogMacroSuffix,mXferContext,fmt,...) CON##mLogMacroSuffix("[0x%02X] "fmt,(mXferContext)->Ep.bEndpointAddress, __VA_ARGS__)

#if defined(BM_USE_LIBUSB10_WIN) || !defined(_WIN32)
/*	libusbM: */
#  include <usb.h>
#else
/*	libusb-win32: Uses a different header because of WDK conflicts. */
#  include "usb.h"
#endif

#if MPL_OS_TYPE == MPL_OS_TYPE_WINDOWS
#  include <conio.h>
#  define PRINTF_I64 "%I64d"
#  define BM_MAIN_CALL _cdecl

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
#  define BM_MAIN_CALL

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
 
  select(STDIN_FILENO+1, &rdfs, NULL, NULL, &tv);
  return FD_ISSET(STDIN_FILENO, &rdfs);
}
#else
#  error "Benchmark application not supported on for this platform."
#endif

// Custom vendor requests that must be implemented in the benchmark firmware.
// Test selection can be bypassed with the "notestselect" argument.
//
enum BM_DEVICE_COMMANDS
{
    SET_TEST = 0x0E,
    GET_TEST = 0x0F,
};

// Tests supported by the official benchmark firmware.
//
enum BM_DEVICE_TEST_TYPE
{
    TestTypeNone	= 0x00,
    TestTypeRead	= 0x01,
    TestTypeWrite	= 0x02,
    TestTypeLoop	= TestTypeRead|TestTypeWrite,
};

// This software was mainly created for testing the libusb-win32 kernel & user driver.
enum BM_TRANSFER_MODE
{
	// Tests for the libusb-win32 sync transfer function.
    TRANSFER_MODE_SYNC,

	// Test for async function, iso transfers, and queued transfers
    TRANSFER_MODE_ASYNC,
};

// Holds all of the information about a test.
struct BM_TEST_PARAM
{
    // User configurable value set from the command line.
    //
    int Vid;			// Vendor ID
    int Pid;			// Porduct ID
    int Intf;			// Interface number
	int	Altf;			// Alt Interface number
    int Ep;				// Endpoint number (1-15)
    int Refresh;		// Refresh interval (ms)
    int Timeout;		// Transfer timeout (ms)
    int Retry;			// Number for times to retry a timed out transfer before aborting
    int BufferSize;		// Number of bytes to transfer
    int BufferCount;	// Number of outstanding asynchronous transfers
    int NoTestSelect;	// If true, don't send control message to select the test type.
    int UseList;		// Show the user a device list and let them choose a benchmark device. 
	int IsoPacketSize; // Isochronous packet size (defaults to the endpoints max packet size)
    int Priority;		// Priority to run this thread at.
	int Verify;		// Only for loop and read test. If true, verifies data integrity. 
	int VerifyDetails;	// If true, prints detailed information for each invalid byte.
    enum BM_DEVICE_TEST_TYPE TestType;	// The benchmark test type.
	enum BM_TRANSFER_MODE TransferMode;	// Sync or Async

    // Internal value use during the test.
    //
    usb_dev_handle* DeviceHandle;
	struct usb_device* Device;
    int IsCancelled;
    int IsUserAborted;

	unsigned char* VerifyBuffer;		// Stores the verify test pattern for 1 packet.
	unsigned short VerifyBufferSize;	// Size of VerifyBuffer
};

// The benchmark transfer context used for asynchronous transfers.  see TransferAsync().
struct BM_TRANSFER_HANDLE
{
	void* Context;
	int InUse;
	unsigned char* Data;
	int DataMaxLength;
	int ReturnCode;
};

// Holds all of the information about a transfer.
struct BM_TRANSFER_PARAM
{
    struct BM_TEST_PARAM* Test;

    MPL_THREAD_T ThreadHandle;
	struct usb_endpoint_descriptor Ep;
	int IsoPacketSize;
    int IsRunning;

    mint64_t TotalTransferred;
	int LastTransferred;

    int Packets;
    double StartTick;
    double LastTick;
    double LastStartTick;

    int TotalTimeoutCount;
    int RunningTimeoutCount;
	
	int TotalErrorCount;
	int RunningErrorCount;

	int ShortTransferCount;

	int TransferHandleNextIndex;
	int TransferHandleWaitIndex;
	int OutstandingTransferCount;

	struct BM_TRANSFER_HANDLE TransferHandles[MAX_OUTSTANDING_TRANSFERS];

	// Placeholder for end of structure; this is where the raw data for the
	// transfer buffer is allocated.
	//
    unsigned char Buffer[1];
};

// Benchmark device api.
struct usb_dev_handle* Bench_Open(int vid, int pid, int interfaceNumber, int altInterfaceNumber, struct usb_device** deviceForHandle);
int Bench_SetTestType(struct usb_dev_handle* dev, enum BM_DEVICE_TEST_TYPE testType, int intf);
int Bench_GetTestType(struct usb_dev_handle* dev, enum BM_DEVICE_TEST_TYPE* testType, int intf);

// Critical section for running status. 
MPL_MUTEX_T g_DisplayMutex;
MPL_SEM_T g_ThreadBarrier;

// Finds the interface for [interface_number] in a libusb-win32 config descriptor.
// If first_interface is not NULL, it is set to the first interface in the config.
//
struct usb_interface_descriptor* usb_find_interface(struct usb_config_descriptor* config_descriptor,
	int interface_number,
	int alt_interface_number,
	struct usb_interface_descriptor** first_interface);

// Internal function used by the benchmark application.
void ShowHelp(void);
void ShowCopyright(void);
void SetTestDefaults(struct BM_TEST_PARAM* test);
char* GetParamStrValue(const char* src, const char* paramName);
int GetParamIntValue(const char* src, const char* paramName, int* returnValue);
int ValidateBenchmarkArgs(struct BM_TEST_PARAM* testParam);
int ParseBenchmarkArgs(struct BM_TEST_PARAM* testParams, int argc, char **argv);
void FreeTransferParam(struct BM_TRANSFER_PARAM** testTransferRef);
struct BM_TRANSFER_PARAM* CreateTransferParam(struct BM_TEST_PARAM* test, int endpointID);
void GetAverageBytesSec(struct BM_TRANSFER_PARAM* transferParam, double* bps);
void GetCurrentBytesSec(struct BM_TRANSFER_PARAM* transferParam, double* bps);
void ShowRunningStatus(struct BM_TRANSFER_PARAM* transferParam);
void ShowTestInfo(struct BM_TEST_PARAM* testParam);
void ShowTransferInfo(struct BM_TRANSFER_PARAM* transferParam);

void ResetRunningStatus(struct BM_TRANSFER_PARAM* transferParam);

// The thread transfer routine.
MPL_THDPROC_RETURN_TYPE MPL_THDPROC_CC TransferThreadProc(void* user_context);

#define TRANSFER_DISPLAY(TransferParam, ReadingString, WritingString) \
	((TransferParam->Ep.bEndpointAddress & USB_ENDPOINT_DIR_MASK) ? ReadingString : WritingString)

#define INC_ROLL(IncField, RollOverValue) if ((++IncField) >= RollOverValue) IncField = 0

#define ENDPOINT_TYPE(TransferParam) (TransferParam->Ep.bmAttributes & 3)
const char* TestDisplayString[] = {"None", "Read", "Write", "Loop", NULL};
const char* EndpointTypeDisplayString[] = {"CONTROL", "ISOCHRONOUS", "BULK", "INTERRUPT", NULL};

char* Str_ToLower(char* s)
{
	char* p = s;
	if (!s || !*s) return s;
	do
	{
		if (*p >= 'A' && *p <= 'Z') *p+=32;
	}while(*(++p) != '\0');
	return s;
}

void SetTestDefaults(struct BM_TEST_PARAM* test)
{
    memset(test,0,sizeof(struct BM_TEST_PARAM));

    test->Ep			= 0x00;
    test->Vid			= 0x0666;
    test->Pid			= 0x0001;
    test->Refresh		= 1000;
    test->Timeout		= 1000;
    test->TestType		= TestTypeLoop;
    test->BufferSize	= 4096;
    test->BufferCount   = 1;
#ifdef _WIN32
    test->Priority		= THREAD_PRIORITY_NORMAL;
#endif
}

struct usb_interface_descriptor* usb_find_interface(struct usb_config_descriptor* config_descriptor,
	int interface_number,
	int alt_interface_number,
	struct usb_interface_descriptor** first_interface)
{
	struct usb_interface_descriptor* intf;
	int intfIndex;

	if (first_interface) 
		*first_interface = NULL;

	if (!config_descriptor) return NULL;

	for (intfIndex = 0; intfIndex < config_descriptor->bNumInterfaces; intfIndex++)
	{
		if (config_descriptor->interface[intfIndex].num_altsetting)
		{
			intf = &config_descriptor->interface[intfIndex].altsetting[0];
			if ((first_interface) && *first_interface == NULL)
				*first_interface = intf;

			if (intf->bInterfaceNumber == interface_number && 
				(alt_interface_number==-1 || intf->bAlternateSetting==alt_interface_number))
			{
				return intf;
			}
		}
	}

	return NULL;
}
struct usb_dev_handle* Bench_Open(int vid, int pid, int interfaceNumber, int altInterfaceNumber, struct usb_device** deviceForHandle)
{
    struct usb_bus* bus;
    struct usb_device* dev;
    struct usb_dev_handle* udev;

    for (bus = usb_get_busses(); bus; bus = bus->next)
    {
        for (dev = bus->devices; dev; dev = dev->next)
        {
            if (dev->descriptor.idVendor == vid && dev->descriptor.idProduct == pid)
            {
				if ((udev = usb_open(dev)) != NULL)
				{
					if (dev->descriptor.bNumConfigurations)
					{
						if (usb_find_interface(&dev->config[0], interfaceNumber, altInterfaceNumber, NULL) != NULL)
						{
							if (deviceForHandle) *deviceForHandle = dev;
							return udev;
						}
					}

					usb_close(udev);
				}
            }
        }
    }
    return NULL;
}

int Bench_SetTestType(struct usb_dev_handle* dev, enum BM_DEVICE_TEST_TYPE testType, int intf)
{
    char buffer[1];
    int ret = 0;

    ret = usb_control_msg(dev,
                          USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_IN,
                          SET_TEST, testType, intf,
                          buffer, 1,
                          1000);
    return ret;
}

int Bench_GetTestType(struct usb_dev_handle* dev, enum BM_DEVICE_TEST_TYPE* testType, int intf)
{
    char buffer[1];
    int ret = 0;

    ret = usb_control_msg(dev,
                          USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_IN,
                          GET_TEST, 0, intf,
                          buffer, 1,
                          1000);

    if (ret == 1)
        *testType = buffer[0];

    return ret;
}
enum TRANSFER_VERIFY_STATE
{
	TVS_START,
	TVS_KEY,
	TVS_DATA,
	TVS_FIND_START,

};

int VerifyData(struct BM_TRANSFER_PARAM* transferParam, unsigned char* data, int dataLength)
{

	int verifyDataSize = transferParam->Test->VerifyBufferSize;
	unsigned char* verifyData = transferParam->Test->VerifyBuffer;
	unsigned char keyC = 0;
	int seedKey = TRUE;
	int dataLeft = dataLength;
	int dataIndex = 0;
	int packetIndex = 0;
	int verifyIndex = 0;

	while(dataLeft > 1)
	{
		verifyDataSize = dataLeft > transferParam->Test->VerifyBufferSize ? transferParam->Test->VerifyBufferSize : dataLeft;

		if (seedKey)
			keyC = data[dataIndex+1];
		else
		{
			if (data[dataIndex+1]==0)
			{
				keyC=0;
			}
			else
			{
				keyC++;
			}
		}
		seedKey = FALSE;
		// Index 0 is always 0.
		// The key is always at index 1
		verifyData[1] = keyC;
		if (memcmp(&data[dataIndex],verifyData,verifyDataSize) != 0)
		{
			// Packet verification failed.

			// Reset the key byte on the next packet.
			seedKey = TRUE;

			CONVDAT("data mismatch packet-index=%d data-index=%d\n", packetIndex, dataIndex);

			if (transferParam->Test->VerifyDetails)
			{
				for (verifyIndex=0; verifyIndex<verifyDataSize; verifyIndex++)
				{
					if (verifyData[verifyIndex] == data[dataIndex + verifyIndex])
						continue;

					CONVDAT("packet-offset=%d expected %02Xh got %02Xh\n",
						verifyIndex,
						verifyData[verifyIndex],
						data[dataIndex+verifyIndex]);

				}
			}

		}

		// Move to the next packet.
		packetIndex++;
		dataLeft -= verifyDataSize;
		dataIndex+= verifyDataSize;

	}

	return 0;
}

int TransferSync(struct BM_TRANSFER_PARAM* transferParam)
{
	int ret;
	if (transferParam->Ep.bEndpointAddress & USB_ENDPOINT_DIR_MASK)
	{
		ret = usb_bulk_read(
				  transferParam->Test->DeviceHandle, transferParam->Ep.bEndpointAddress,
				  (char*)&transferParam->Buffer[0], transferParam->Test->BufferSize,
				  transferParam->Test->Timeout);
	}
	else
	{
		ret = usb_bulk_write(
				  transferParam->Test->DeviceHandle, transferParam->Ep.bEndpointAddress,
				  (char*)&transferParam->Buffer[0], transferParam->Test->BufferSize,
				  transferParam->Test->Timeout);
	}

	return ret;
}

int TransferAsync(struct BM_TRANSFER_PARAM* transferParam, struct BM_TRANSFER_HANDLE** handleRef)
{
	int ret = -EFAULT;
	struct BM_TRANSFER_HANDLE* handle;

	*handleRef = NULL;

	// Submit transfers until the maximum number of outstanding transfer(s) is reached.
	while (transferParam->OutstandingTransferCount < transferParam->Test->BufferCount)
	{
		// Get the next available benchmark transfer handle.
		*handleRef = handle = &transferParam->TransferHandles[transferParam->TransferHandleNextIndex];

		// If a libusb-win32 transfer context hasn't been setup for this benchmark transfer
		// handle, do it now.
		//
		if (!handle->Context)
		{
			// Data buffer(s) are located at the end of the transfer param.
			handle->Data = transferParam->Buffer + (transferParam->TransferHandleNextIndex * transferParam->Test->BufferSize);
			handle->DataMaxLength = transferParam->Test->BufferSize;

			
			switch (ENDPOINT_TYPE(transferParam))
			{
			case USB_ENDPOINT_TYPE_ISOCHRONOUS:
				ret = usb_isochronous_setup_async(transferParam->Test->DeviceHandle, 
					&handle->Context,
					transferParam->Ep.bEndpointAddress,
					transferParam->IsoPacketSize ? transferParam->IsoPacketSize : transferParam->Ep.wMaxPacketSize);
				break;
			case USB_ENDPOINT_TYPE_BULK:
				ret = usb_bulk_setup_async(transferParam->Test->DeviceHandle,
					&handle->Context,
					transferParam->Ep.bEndpointAddress);
				break;
			case USB_ENDPOINT_TYPE_INTERRUPT:
				ret = usb_interrupt_setup_async(transferParam->Test->DeviceHandle,
					&handle->Context,
					transferParam->Ep.bEndpointAddress);
				break;
			default:
				ret = -1;
				break;
			}

			if (ret < 0) 
			{
				CONMSG("failed creating transfer context ret=%d\n",ret);
				goto Done;
			}
		}


		// Submit this transfer now.
		handle->ReturnCode = ret = usb_submit_async(handle->Context, (char*)&handle->Data[0], handle->DataMaxLength);
		if (ret < 0)
		{
			if (!transferParam->Test->IsCancelled)
			{
				XFERLOG(ERR, transferParam, "Submit transfer failed. ret=%d\n", ret);
			}
			goto Done;
		}
			// Mark this handle has InUse.
		handle->InUse = TRUE;

		// When transfers ir successfully submitted, OutstandingTransferCount goes up; when
		// they are completed it goes down.
		//
		transferParam->OutstandingTransferCount++;

		// Move TransferHandleNextIndex to the next available transfer.
		INC_ROLL(transferParam->TransferHandleNextIndex, transferParam->Test->BufferCount);

	}

	// If the number of outstanding transfers has reached the limit, wait for the 
	// oldest outstanding transfer to complete.
	//
	if (transferParam->OutstandingTransferCount == transferParam->Test->BufferCount)
	{
		// TransferHandleWaitIndex is the index of the oldest outstanding transfer.
		*handleRef = handle = &transferParam->TransferHandles[transferParam->TransferHandleWaitIndex];

		// Only wait, cancelling & freeing is handled by the caller.
		handle->ReturnCode = ret = usb_reap_async_nocancel(handle->Context, transferParam->Test->Timeout);

		if (ret < 0) 
		{
			if (!transferParam->Test->IsCancelled && (ret != -ETIMEDOUT))
			{
				XFERLOG(ERR, transferParam, "Reap transfer failed. ret=%d\n", ret);
			}
			goto Done;
		}

		// Mark this handle has no longer InUse.
		handle->InUse = FALSE;

		// When transfers ir successfully submitted, OutstandingTransferCount goes up; when
		// they are completed it goes down.
		//
		transferParam->OutstandingTransferCount--;

		// Move TransferHandleWaitIndex to the oldest outstanding transfer.
		INC_ROLL(transferParam->TransferHandleWaitIndex, transferParam->Test->BufferCount);
	}

Done:
	return ret;
}

MPL_THDPROC_RETURN_TYPE MPL_THDPROC_CC TransferThreadProc(void* user_context)
{
	int i;
	int r;
    int transferLength;
	struct BM_TRANSFER_HANDLE* handle;
	unsigned char* data;
	struct BM_TRANSFER_PARAM* transferParam = (struct BM_TRANSFER_PARAM*)user_context;
    transferParam->IsRunning = TRUE;

	Mpl_Sem_Wait(&g_ThreadBarrier);

	while (!transferParam->Test->IsCancelled)
    {
		data = NULL;
		handle = NULL;

		if (transferParam->Test->TransferMode == TRANSFER_MODE_SYNC)
		{
			r = TransferSync(transferParam);
			if (r >= 0) data = transferParam->Buffer;
		}
		else if (transferParam->Test->TransferMode == TRANSFER_MODE_ASYNC)
		{
			r = TransferAsync(transferParam, &handle);
			if ((handle) && r >= 0) data = handle->Data;
		}
		else
		{
            CONERR("invalid transfer mode %d\n",transferParam->Test->TransferMode);
			goto Done;
		}
        if (r < 0)
        {
			transferLength = 0;
			// Transfer timed out
            if (r == -ETIMEDOUT)
            {
                transferParam->TotalTimeoutCount++;
                transferParam->RunningTimeoutCount++;
				XFERLOG(WRN, transferParam, "Timeout #%d..\n", transferParam->RunningTimeoutCount);

				// The user pressed 'Q'.
				if (transferParam->Test->IsUserAborted) break;

                if (transferParam->RunningTimeoutCount > transferParam->Test->Retry)
                    break;
            }
            else
            {
				// The user pressed 'Q'.
				if (transferParam->Test->IsUserAborted) break;

				// An error (other than a timeout) occured.
				transferParam->TotalErrorCount++;
                transferParam->RunningErrorCount++;
				XFERLOG(ERR, transferParam, "Transfer failed. (%d of %d) ret=%d:\n\t%s\n", 
					transferParam->RunningErrorCount, transferParam->Test->Retry+1, r, usb_strerror());

                if (transferParam->RunningErrorCount > transferParam->Test->Retry)
                    break;

            }
        }
        else
        {
			transferLength = r;
			if (transferLength < transferParam->Test->BufferSize && !transferParam->Test->IsCancelled)
			{
				XFERLOG(WRN, transferParam, "Short transfer. expected %d got %d.\n",
					transferParam->Test->BufferSize, transferLength);

				if (transferLength > 0)
				{
					transferParam->ShortTransferCount++;
				}
				else
				{
					transferParam->TotalErrorCount++;
					transferParam->RunningErrorCount++;
					if (transferParam->RunningErrorCount > transferParam->Test->Retry)
						break;
				}
			}
			else
			{
				transferParam->RunningErrorCount = 0;
				transferParam->RunningTimeoutCount = 0;
			}

			if ((transferParam->Test->Verify) && 
				(transferParam->Ep.bEndpointAddress & USB_ENDPOINT_DIR_MASK))
			{
				VerifyData(transferParam, data, transferLength);
			}
        }

		if ((r = Mpl_Mutex_Wait(&g_DisplayMutex)) != MPL_SUCCESS)
		{
			CONERR("Mpl_Mutex_Wait failed. ret=%d\n",r);
			goto Done;
		}

        if (!transferParam->StartTick && transferParam->Packets >= 0)
        {
			transferParam->StartTick = Mpl_Clock_Ticks();
			transferParam->LastStartTick = transferParam->StartTick;
            transferParam->LastTick = transferParam->StartTick;

			transferParam->LastTransferred = 0;
            transferParam->TotalTransferred = 0;
            transferParam->Packets = 0;

        }
        else
        {
			if (!transferParam->LastStartTick)
			{
				transferParam->LastStartTick = transferParam->LastTick;
				transferParam->LastTransferred = 0;
			}

            transferParam->LastTick = Mpl_Clock_Ticks();
 
			transferParam->LastTransferred  += transferLength;
            transferParam->TotalTransferred += transferLength;
            transferParam->Packets++;
        }

		Mpl_Mutex_Release(&g_DisplayMutex);
    }

Done:

	for (i=0; i < transferParam->Test->BufferCount; i++)
	{
		if (transferParam->TransferHandles[i].Context)
		{
			if (transferParam->TransferHandles[i].InUse)
			{
				int r_cancel;
				if ((r_cancel = usb_cancel_async(transferParam->TransferHandles[i].Context)) < 0)
				{
					if (!transferParam->Test->IsUserAborted)
					{
						XFERLOG(ERR, transferParam, "Cancel transfer failed. ret=%d\n",r_cancel);
					}
				}
				else
				{
					usb_reap_async_nocancel(transferParam->TransferHandles[i].Context, INFINITE);
				}

				transferParam->TransferHandles[i].InUse=FALSE;
			}
			usb_free_async(&transferParam->TransferHandles[i].Context);
		}
	}

	transferParam->IsRunning = FALSE;
	Mpl_Sem_Release(&g_ThreadBarrier);

	XFERLOG(MSG, transferParam, "Thread stopped. user-abort=%c\n", transferParam->Test->IsUserAborted ? 'Y':'N');
    return (MPL_THDPROC_RETURN_TYPE)NULL;
}

char* GetParamStrValue(const char* src, const char* paramName)
{
	return (strstr(src,paramName)==src) ? (char*)(src+strlen(paramName)) : NULL;
}

int GetParamIntValue(const char* src, const char* paramName, int* returnValue)
{
    char* value = GetParamStrValue(src, paramName);
    if (value)
    {
        *returnValue = strtol(value, NULL, 0);
        return TRUE;
    }
    return FALSE;
}

int ValidateBenchmarkArgs(struct BM_TEST_PARAM* testParam)
{
    if (testParam->BufferCount < 1 || testParam->BufferCount > MAX_OUTSTANDING_TRANSFERS)
    {
		CONERR("Invalid BufferCount argument %d. BufferCount must be greater than 0 and less than or equal to %d.\n",
			testParam->BufferCount, MAX_OUTSTANDING_TRANSFERS);
        return -1;
    }

    return 0;
}

int ParseBenchmarkArgs(struct BM_TEST_PARAM* testParams, int argc, char **argv)
{
#define GET_INT_VAL
    char arg[128];
    char* value;
    int iarg;

    for (iarg=1; iarg < argc; iarg++)
    {
		if (strlen(argv[iarg]) >= sizeof(arg))
			return -1;
		else
			strcpy(arg, argv[iarg]);

        Str_ToLower(arg);

        if      (GetParamIntValue(arg, "vid=", &testParams->Vid)) {}
        else if (GetParamIntValue(arg, "pid=", &testParams->Pid)) {}
        else if (GetParamIntValue(arg, "retry=", &testParams->Retry)) {}
        else if (GetParamIntValue(arg, "buffercount=", &testParams->BufferCount)) 
		{
			if (testParams->BufferCount > 1)
				testParams->TransferMode = TRANSFER_MODE_ASYNC;
		}
        else if (GetParamIntValue(arg, "buffersize=", &testParams->BufferSize)) {}
        else if (GetParamIntValue(arg, "size=", &testParams->BufferSize)) {}
        else if (GetParamIntValue(arg, "timeout=", &testParams->Timeout)) {}
        else if (GetParamIntValue(arg, "intf=", &testParams->Intf)) {}
        else if (GetParamIntValue(arg, "altf=", &testParams->Altf)) {}
        else if (GetParamIntValue(arg, "ep=", &testParams->Ep)) 
		{
			testParams->Ep &= 0xf;
		}
        else if (GetParamIntValue(arg, "refresh=", &testParams->Refresh)) {}
        else if (GetParamIntValue(arg, "isopacketsize=", &testParams->IsoPacketSize)) {}
        else if ((value=GetParamStrValue(arg,"mode="))!=NULL)
        {
            if (GetParamStrValue(value,"sync"))
            {
				testParams->TransferMode = TRANSFER_MODE_SYNC;
            }
            else if (GetParamStrValue(value,"async"))
            {
				testParams->TransferMode = TRANSFER_MODE_ASYNC;
            }
             else
            {
                // Invalid EndpointType argument.
                CONERR("invalid transfer mode argument! %s\n",argv[iarg]);
                return -1;

            }
        }
        else if ((value=GetParamStrValue(arg,"priority="))!=NULL)
        {
#ifdef _WIN32
            if (GetParamStrValue(value,"lowest"))
            {
                testParams->Priority=THREAD_PRIORITY_LOWEST;
            }
            else if (GetParamStrValue(value,"belownormal"))
            {
                testParams->Priority=THREAD_PRIORITY_BELOW_NORMAL;
            }
            else if (GetParamStrValue(value,"normal"))
            {
                testParams->Priority=THREAD_PRIORITY_NORMAL;
            }
            else if (GetParamStrValue(value,"abovenormal"))
            {
                testParams->Priority=THREAD_PRIORITY_ABOVE_NORMAL;
            }
            else if (GetParamStrValue(value,"highest"))
            {
                testParams->Priority=THREAD_PRIORITY_HIGHEST;
            }
            else
            {
                CONERR("invalid priority argument! %s\n",argv[iarg]);
                return -1;
            }
#else
            CONWRN("multi-platform thread priorty not yet implemented.\n");
#endif
        }
        else if (!strcmp(arg,"notestselect"))
        {
            testParams->NoTestSelect = TRUE;
        }
        else if (!strcmp(arg,"read"))
        {
            testParams->TestType = TestTypeRead;
        }
        else if (!strcmp(arg,"write"))
        {
            testParams->TestType = TestTypeWrite;
        }
        else if (!strcmp(arg,"loop"))
        {
            testParams->TestType = TestTypeLoop;
        }
        else if (!strcmp(arg,"list"))
        {
            testParams->UseList = TRUE;
        }
        else if (!strcmp(arg,"verifydetails"))
        {
            testParams->VerifyDetails = TRUE;
            testParams->Verify = TRUE;
        }
        else if (!strcmp(arg,"verify"))
        {
            testParams->Verify = TRUE;
        }
		else
        {
            CONERR("invalid argument! %s\n",argv[iarg]);
            return -1;
        }
    }
    return ValidateBenchmarkArgs(testParams);
}

int CreateVerifyBuffer(struct BM_TEST_PARAM* testParam, unsigned short endpointMaxPacketSize)
{
	int i;
	unsigned char indexC = 0;
	testParam->VerifyBuffer = malloc(endpointMaxPacketSize);
	if (!testParam->VerifyBuffer)
	{
        CONERR("memory allocation failure at line %d!\n",__LINE__);
        return -1;
	}

	testParam->VerifyBufferSize = endpointMaxPacketSize;

	for(i=0; i < endpointMaxPacketSize; i++)
	{
	   testParam->VerifyBuffer[i] = indexC++;
	   if (indexC == 0) indexC = 1;
	}

	return 0;
}

void FreeTransferParam(struct BM_TRANSFER_PARAM** testTransferRef)
{
	struct BM_TRANSFER_PARAM* pTransferParam;

	if ((!testTransferRef) || !*testTransferRef) return;
	pTransferParam = *testTransferRef;

    free(pTransferParam);

    *testTransferRef = NULL;
}

struct BM_TRANSFER_PARAM* CreateTransferParam(struct BM_TEST_PARAM* test, int endpointID)
{
    struct BM_TRANSFER_PARAM* transferParam;
	struct usb_interface_descriptor* testInterface;
	int i;
    int allocSize = sizeof(struct BM_TRANSFER_PARAM)+(test->BufferSize * test->BufferCount);

    transferParam = (struct BM_TRANSFER_PARAM*) malloc(allocSize);

    if (transferParam)
    {
        memset(transferParam, 0, allocSize);
        transferParam->Test = test;
		if ((testInterface = usb_find_interface(&test->Device->config[0], test->Intf, test->Altf, NULL))==NULL)
		{
            CONERR("failed locating interface %02Xh!\n", test->Intf);
            FreeTransferParam(&transferParam);
			goto Done;
		}

		for(i=0; i < testInterface->bNumEndpoints; i++)
		{
			if (!(endpointID & USB_ENDPOINT_ADDRESS_MASK))
			{
				// Use first endpoint that matches the direction
				if ((testInterface->endpoint[i].bEndpointAddress & USB_ENDPOINT_DIR_MASK) == endpointID)
				{
					memcpy(&transferParam->Ep, &testInterface->endpoint[i],sizeof(struct usb_endpoint_descriptor));
					break;
				}
			}
			else
			{
				if ((int)testInterface->endpoint[i].bEndpointAddress == endpointID)
				{
					memcpy(&transferParam->Ep, &testInterface->endpoint[i],sizeof(struct usb_endpoint_descriptor));
					break;
				}
			}
		}
        if (!transferParam->Ep.bEndpointAddress)
        {
            CONERR("failed locating EP%02Xh!\n", endpointID);
            FreeTransferParam(&transferParam);
			goto Done;
        }

        if (transferParam->Test->BufferSize % transferParam->Ep.wMaxPacketSize)
        {
            CONERR("buffer size %d is not an interval of EP%02Xh maximum packet size of %d!\n",
				transferParam->Test->BufferSize,
				transferParam->Ep.bEndpointAddress,
				transferParam->Ep.wMaxPacketSize);

            FreeTransferParam(&transferParam);
			goto Done;
        }

		if (test->IsoPacketSize)
			transferParam->IsoPacketSize = test->IsoPacketSize;
		else
			transferParam->IsoPacketSize = transferParam->Ep.wMaxPacketSize;

		if (ENDPOINT_TYPE(transferParam) == USB_ENDPOINT_TYPE_ISOCHRONOUS)
			transferParam->Test->TransferMode = TRANSFER_MODE_ASYNC;

        ResetRunningStatus(transferParam);

		// If verify mode is on, this is a loop test, and this is a write endpoint, fill
		// the buffers with the same test data sent by a benchmark device when running
		// a read only test.
		if (transferParam->Test->Verify &&
			transferParam->Test->TestType == TestTypeLoop &&
			!(transferParam->Ep.bEndpointAddress & USB_ENDPOINT_DIR_MASK))
		{
			// Data Format:
			// [0][KeyByte] 2 3 4 5 ..to.. wMaxPacketSize (if data byte rolls it is incremented to 1)
			// Increment KeyByte and repeat
			//
			unsigned char indexC=0;
			int bufferIndex = 0;
			unsigned short dataIndex;
			int packetIndex;
			int packetCount = ((transferParam->Test->BufferCount*transferParam->Test->BufferSize) / transferParam->Ep.wMaxPacketSize);
			for(packetIndex = 0; packetIndex < packetCount; packetIndex++)
			{
				indexC = 2;
				for (dataIndex=0; dataIndex < transferParam->Ep.wMaxPacketSize; dataIndex++)
				{
					if (dataIndex == 0)			// Start
						transferParam->Buffer[bufferIndex] = 0;
					else if (dataIndex == 1)	// Key
						transferParam->Buffer[bufferIndex] = packetIndex & 0xFF;
					else						// Data
						transferParam->Buffer[bufferIndex] = indexC++;

					// if wMaxPacketSize is > 255, indexC resets to 1.
					if (indexC == 0) indexC = 1;

					bufferIndex++;
				}
			}
		}
    }

Done:
    if (!transferParam)
        CONERR("failed creating transfer param!\n");

    return transferParam;
}

void GetAverageBytesSec(struct BM_TRANSFER_PARAM* transferParam, double* bps)
{
	double ticksSec;
    if ((!transferParam->StartTick) || 
		(transferParam->StartTick >= transferParam->LastTick) || 
		transferParam->TotalTransferred==0)
    {
        *bps=0;
    }
    else
    {
		ticksSec = (transferParam->LastTick - transferParam->StartTick);
		*bps = (transferParam->TotalTransferred / ticksSec);
    }
}

void GetCurrentBytesSec(struct BM_TRANSFER_PARAM* transferParam, double* bps)
{
	double ticksSec;
    if ((!transferParam->StartTick) || 
		(!transferParam->LastStartTick) || 
		(transferParam->LastTick <= transferParam->LastStartTick) || 
		transferParam->LastTransferred==0)
    {
        *bps=0;
    }
    else
    {
		ticksSec = (transferParam->LastTick - transferParam->LastStartTick);
		*bps = (double)transferParam->LastTransferred / ticksSec;
    }
}

void ShowRunningStatus(struct BM_TRANSFER_PARAM* transferParam)
{
	struct BM_TRANSFER_PARAM temp;
	double bpsOverall;
	double bpsLastTransfer;
	int ret;
    
	// LOCK the display critical section
	if ((ret = Mpl_Mutex_Wait(&g_DisplayMutex)) != MPL_SUCCESS)
	{
		CONERR("Mpl_Mutex_Wait failed. ret=%d\n",ret);
		return;
	}
	memcpy(&temp, transferParam, sizeof(struct BM_TRANSFER_PARAM));
	Mpl_Mutex_Release(&g_DisplayMutex);

    if ((!temp.StartTick) || (temp.StartTick >= temp.LastTick))
    {
        CONMSG("Synchronizing %i. StartTicks=%f..\n", abs(transferParam->Packets), temp.StartTick);
    }
    else
    {
        GetAverageBytesSec(&temp,&bpsOverall);
        GetCurrentBytesSec(&temp,&bpsLastTransfer);
		transferParam->LastStartTick = 0;
		CONMSG("Avg. Bytes/s: %.2f Transfers: %i Bytes/s: %.2f\n",
			bpsOverall, temp.Packets, bpsLastTransfer);
    }

}
void ShowTransferInfo(struct BM_TRANSFER_PARAM* transferParam)
{
    double bpsAverage;
    double bpsCurrent;
    double elapsedSeconds;

	if (!transferParam) return;

	XFERLOG(MSG,transferParam, "%s wMaxPacketSize = 0x%04X (%d x %d) [%s]\n",
		TRANSFER_DISPLAY(transferParam,"Read   (IN)","Write (OUT)"),
		transferParam->Ep.wMaxPacketSize,
		transferParam->Ep.wMaxPacketSize & 0x7FF,
		((transferParam->Ep.wMaxPacketSize & 0x1800) >> 11)+1,
		EndpointTypeDisplayString[ENDPOINT_TYPE(transferParam)]);

	if (transferParam->StartTick)
    {
        GetAverageBytesSec(transferParam,&bpsAverage);
        GetCurrentBytesSec(transferParam,&bpsCurrent);
        CONMSG("\tTotal Bytes     : "PRINTF_I64"\n", transferParam->TotalTransferred);
        CONMSG("\tTotal Transfers : %i\n", transferParam->Packets);

		if (transferParam->ShortTransferCount)
		{
			CONMSG("\tShort Transfers : %d\n", transferParam->ShortTransferCount);
		}
		if (transferParam->TotalTimeoutCount)
		{
			CONMSG("\tTimeout Errors  : %d\n", transferParam->TotalTimeoutCount);
		}
		if (transferParam->TotalErrorCount)
		{
			CONMSG("\tOther Errors    : %d\n", transferParam->TotalErrorCount);
		}

        CONMSG("\tAvg. Bytes/sec  : %.2f\n", bpsAverage);

		if (transferParam->StartTick && transferParam->StartTick < transferParam->LastTick)
		{
			elapsedSeconds = (transferParam->LastTick - transferParam->StartTick);

			CONMSG("\tElapsed Time    : %.2f seconds\n", elapsedSeconds);
		}

	    CONMSG("\n");
    }

}

void ShowTestInfo(struct BM_TEST_PARAM* testParam)
{
    if (!testParam) return;

    CONMSG("%s Test Information\n",TestDisplayString[testParam->TestType & 3]);
    CONMSG("\tVid / Pid       : %04Xh / %04Xh\n", testParam->Vid,  testParam->Pid);
    CONMSG("\tInterface #     : %02Xh\n", testParam->Intf);
    CONMSG("\tPriority        : %d\n", testParam->Priority);
    CONMSG("\tBuffer Size     : %d\n", testParam->BufferSize);
    CONMSG("\tBuffer Count    : %d\n", testParam->BufferCount);
    CONMSG("\tDisplay Refresh : %d (ms)\n", testParam->Refresh);
    CONMSG("\tTransfer Timeout: %d (ms)\n", testParam->Timeout);
    CONMSG("\tRetry Count     : %d\n", testParam->Retry);
    CONMSG("\tVerify Data     : %s%s\n",
		testParam->Verify ? "On" : "Off",
		(testParam->Verify && testParam->VerifyDetails) ? " (Detailed)" : "");

    CONMSG("\n");
}

void ResetRunningStatus(struct BM_TRANSFER_PARAM* transferParam)
{
    if (!transferParam) return;

    transferParam->StartTick=0;
    transferParam->TotalTransferred=0;
    transferParam->Packets=-2;
    transferParam->LastTick=0;
    transferParam->RunningTimeoutCount=0;
}

int GetTestDeviceFromList(struct BM_TEST_PARAM* testParam)
{
    const int LINE_MAX_SIZE   = 1024;
    const int STRING_MAX_SIZE = 256;
    const int NUM_STRINGS = 3;
    const int ALLOC_SIZE = LINE_MAX_SIZE + (STRING_MAX_SIZE * NUM_STRINGS);
	char keyBuf[8];

    int userInput;

    char* buffer;
    char* line;
    char* product;
    char* manufacturer;
    char* serial;

    struct usb_bus* bus;
    struct usb_device* dev;
    usb_dev_handle* udev;
    struct usb_device* validDevices[256];

    int deviceIndex=0;
	struct usb_interface_descriptor* firstInterface;
	int keyIndex;
    int ret = -1;

    buffer = malloc(ALLOC_SIZE);
    if (!buffer)
    {
        CONERR("failed allocating memory!\n");
        return ret;
    }

    line = buffer;
    product = buffer + LINE_MAX_SIZE;
    manufacturer = product + STRING_MAX_SIZE;
    serial = manufacturer + STRING_MAX_SIZE;

    for (bus = usb_get_busses(); bus; bus = bus->next)
    {
        for (dev = bus->devices; dev; dev = dev->next)
        {

            udev = usb_open(dev);
            if (udev)
            {
                memset(buffer, 0, ALLOC_SIZE);
                line = buffer;
                if (dev->descriptor.iManufacturer)
                {
                    if (usb_get_string_simple(udev, dev->descriptor.iManufacturer, manufacturer, STRING_MAX_SIZE - 1) > 0)
                    {
                        strcat(line,"(");
                        strcat(line,manufacturer);
                        strcat(line,") ");
                    }
                }

                if (dev->descriptor.iProduct)
                {
                    if (usb_get_string_simple(udev, dev->descriptor.iProduct, product, STRING_MAX_SIZE - 1) > 0)
                    {
                        strcat(line,product);
                        strcat(line," ");
                    }
                }

                if (dev->descriptor.iSerialNumber)
                {
                    if (usb_get_string_simple(udev, dev->descriptor.iSerialNumber, serial, STRING_MAX_SIZE - 1) > 0)
                    {
                        strcat(line,"[");
                        strcat(line,serial);
                        strcat(line,"] ");
                    }
                }

                if (!deviceIndex)
                    CONMSG("\n");

                validDevices[deviceIndex++] = dev;

                CONMSG("%d. %04X:%04X %s\n",
                          deviceIndex, dev->descriptor.idVendor, dev->descriptor.idProduct, line);

                usb_close(udev);
            }
        }
    }
    
	if (!deviceIndex) 
	{
		CONERR("No devices where found!\n");
		ret = -1;
		goto Done;
	}

	while(ConIO_IsKeyAvailable()) ret = ConIO_GetCh();
	CONMSG("\nSelect device (1-%d) :",deviceIndex);
	for(keyIndex = 0; keyIndex < 2; keyIndex++)
	{
		ret = ConIO_GetCh();
		if (ret < '0' || ret > '9')
		{
			if (ret == 8) // backspace
			{
				if (keyIndex > 0)
				{
					keyIndex--;
					keyBuf[keyIndex]='\0';
					LOG("\x08 \x08");
				}
				keyIndex--;
				continue;
			}
			else if (keyIndex > 0 && (ret == '\n' || ret == '\r')) break;

			ret = -1;
			break;
		}
		LOG("%c",ret);
		keyBuf[keyIndex] = (char)ret;
		ret = 0;
	}
	keyBuf[keyIndex] = '\0';


	/* Terminate string with null character: */

    if (ret == -1)
	{
        CONMSG("\n");
        CONMSG("Aborting..\n");
		goto Done;
	}
    CONMSG("\n");

	userInput = atoi(keyBuf) - 1;
	ret = -1;
   if (userInput >= 0 && userInput < deviceIndex)
    {
        testParam->DeviceHandle = usb_open(validDevices[userInput]);
        if (testParam->DeviceHandle)
        {
			testParam->Device = validDevices[userInput];
            testParam->Vid = testParam->Device->descriptor.idVendor;
            testParam->Pid = testParam->Device->descriptor.idProduct;
			if (usb_find_interface(&validDevices[userInput]->config[0],testParam->Intf, testParam->Altf, &firstInterface) == NULL)
			{
				// the specified (or default) interface didn't exist, use the first one.
				if (firstInterface != NULL)
				{
					testParam->Intf = firstInterface->bInterfaceNumber;
				}
				else
				{
					CONERR("device %04X:%04X does not have any interfaces!\n",
						testParam->Vid, testParam->Pid);
					ret = -1;
					goto Done;
				}
			}
            ret = 0;
        }
	}

Done:
	if (buffer)
        free(buffer);

    return ret;
}

int BM_MAIN_CALL main(int argc, char** argv)
{
    struct BM_TEST_PARAM Test;
    struct BM_TRANSFER_PARAM* ReadTest	= NULL;
    struct BM_TRANSFER_PARAM* WriteTest	= NULL;
    int key;
	int ret;
	int threadCount = 0;


    if (argc == 1)
    {
        ShowHelp();
        return -1;
    }

	ShowCopyright();

    SetTestDefaults(&Test);

    // Load the command line arguments.
    if (ParseBenchmarkArgs(&Test, argc, argv) < 0)
        return -1;

	// Initialize Mpl threads.
	Mpl_Init();

	memset(&g_DisplayMutex,0,sizeof(g_DisplayMutex));
	memset(&g_ThreadBarrier,0,sizeof(g_ThreadBarrier));

    // Initialize the mutex used for locking the running statistics.
	if ((ret = Mpl_Mutex_Init(&g_DisplayMutex))!=MPL_SUCCESS)
	{
		CONERR("Mpl_Mutex_Init failed. ret=%d\n",ret);
        return -1;
	}
    // Initialize the mutex used for locking the running statistics.
	if ((ret = Mpl_Sem_Init(&g_ThreadBarrier, 0))!=MPL_SUCCESS)
	{
		CONERR("Mpl_Sem_Init failed. ret=%d\n",ret);
        return -1;
	}
	// Set the debug level.
	usb_set_debug(3);

    // Initialize the library.
    usb_initex(NULL);

    // Find all busses.
    usb_find_busses();

    // Find all connected devices.
    usb_find_devices();

	// Disable keyboard input echo on linux/mac.
	ConIO_EchoInput_Disabled();

    if (Test.UseList)
    {
        if (GetTestDeviceFromList(&Test) < 0)
            goto Done;
    }
    else
    {
        // Open a benchmark device. see Bench_Open().
        Test.DeviceHandle = Bench_Open(Test.Vid, Test.Pid, Test.Intf, Test.Altf, &Test.Device);
    }
    if (!Test.DeviceHandle || !Test.Device)
    {
        CONERR("device %04X:%04X not found!\n",Test.Vid, Test.Pid);
        goto Done;
    }

    // If "NoTestSelect" appears in the command line then don't send the control
    // messages for selecting the test type.
    //
    if (!Test.NoTestSelect)
    {
        if (Bench_SetTestType(Test.DeviceHandle, Test.TestType, Test.Intf) != 1)
        {
            CONERR("setting bechmark test type #%d!\n%s\n", Test.TestType, usb_strerror());
            goto Done;
        }
    }

    CONMSG("Benchmark device %04X:%04X opened..\n",Test.Vid, Test.Pid);

    // If reading from the device create the read transfer param. This will also create
    // a thread in a suspended state.
    //
    if (Test.TestType & TestTypeRead)
    {
		Mpl_Sem_Release(&g_ThreadBarrier);
        ReadTest = CreateTransferParam(&Test, Test.Ep | USB_ENDPOINT_DIR_MASK);
        if (!ReadTest) goto Done;
    }

    // If writing to the device create the write transfer param. This will also create
    // a thread in a suspended state.
    //
    if (Test.TestType & TestTypeWrite)
    {
		Mpl_Sem_Release(&g_ThreadBarrier);
        WriteTest = CreateTransferParam(&Test, Test.Ep);
        if (!WriteTest) goto Done;
    }

    // Set configuration #1.
    if (usb_set_configuration(Test.DeviceHandle, 1) < 0)
    {
        CONERR("setting configuration #%d!\n%s\n",1,usb_strerror());
        goto Done;
    }

    // Claim_interface Test.Intf (Default is #0)
    if (usb_claim_interface(Test.DeviceHandle, Test.Intf) < 0)
    {
        CONERR("claiming interface #%d!\n%s\n", Test.Intf, usb_strerror());
        goto Done;
    }

    // Set the alternate setting (Default is #0)
	if (usb_set_altinterface(Test.DeviceHandle, Test.Altf) < 0)
	{
		CONERR("selecting alternate setting #%d on interface #%d!\n%s\n", Test.Altf,  Test.Intf, usb_strerror());
        goto Done;
	}
	else
	{
		if (Test.Altf > 0)
		{
			CONDBG("selected alternate setting #%d on interface #%d\n",Test.Altf,  Test.Intf);
		}
	}

	if (Test.Verify)
	{
		if (ReadTest && WriteTest)
		{
			if (CreateVerifyBuffer(&Test, WriteTest->Ep.wMaxPacketSize) < 0)
				goto Done;
		}
		else if (ReadTest)
		{
			if (CreateVerifyBuffer(&Test, ReadTest->Ep.wMaxPacketSize) < 0)
				goto Done;
		}
	}

	ShowTestInfo(&Test);
	ShowTransferInfo(ReadTest);
	ShowTransferInfo(WriteTest);

	CONMSG("\nWhile the test is running:\n");
	CONMSG("Press 'Q' to quit\n");
	CONMSG("Press 'T' for test details\n");
	CONMSG("Press 'I' for status information\n");
	CONMSG("Press 'R' to reset averages\n");
    CONMSG("\nPress 'Q' to exit, any other key to begin..");
	while(ConIO_IsKeyAvailable()) ConIO_GetCh();
    key = ConIO_GetCh();
    CONMSG("\n");

    if (key=='Q' || key=='q') goto Done;

    // thread priority, create, start.
	// TODO: Add thread priority to Mpl
    if (ReadTest)
    {
		threadCount++;
		if ((ret = Mpl_Thread_Init(&ReadTest->ThreadHandle, TransferThreadProc, ReadTest)) != MPL_SUCCESS)
		{
			CONERR("Mpl_Thread_Init failed. ret=%d\n", ret);
			goto Done;
		}
    }

    // thread priority, create, start.
    if (WriteTest)
    {
		threadCount++;
 		if ((ret = Mpl_Thread_Init(&WriteTest->ThreadHandle, TransferThreadProc, WriteTest)) != MPL_SUCCESS)
		{
			CONERR("Mpl_Thread_Init failed. ret=%d\n", ret);
			goto Done;
		}
    }

    while (!Test.IsCancelled)
    {
        MPL_SleepMs(Test.Refresh);

        if (ConIO_IsKeyAvailable())
        {
            // A key was pressed.
            key = ConIO_GetCh();
            switch (key)
            {
            case 'Q':
            case 'q':
				CONMSG("stopping test..\n");

                Test.IsUserAborted = TRUE;
                Test.IsCancelled = TRUE;
                break;
			case 'T':
			case 't':
				ShowTestInfo(&Test);
				break;
            case 'I':
            case 'i':
                // LOCK the display critical section
				if ((ret = Mpl_Mutex_Wait(&g_DisplayMutex)) != MPL_SUCCESS)
				{
					CONERR("Mpl_Mutex_Wait failed. ret=%d\n",ret);
				}
				else
				{
					// Print benchmark test details.
					ShowTransferInfo(ReadTest);
					ShowTransferInfo(WriteTest);


					// UNLOCK the display critical section
					Mpl_Mutex_Release(&g_DisplayMutex);
				}
                break;

            case 'R':
            case 'r':
                // LOCK the display critical section
				if ((ret = Mpl_Mutex_Wait(&g_DisplayMutex)) != MPL_SUCCESS)
				{
					CONERR("Mpl_Mutex_Wait failed. ret=%d\n",ret);
				}
				else
				{
					// Reset the running status.
					ResetRunningStatus(ReadTest);
					ResetRunningStatus(WriteTest);

					// UNLOCK the display critical section
					Mpl_Mutex_Release(&g_DisplayMutex);
				}
                break;
            }

            // Only one key at a time.
            while (ConIO_IsKeyAvailable()) ConIO_GetCh();
        }

        // If the read test should be running and it isn't, cancel the test.
        if ((ReadTest) && !ReadTest->IsRunning)
        {
            Test.IsCancelled = TRUE;
            break;
        }

        // If the write test should be running and it isn't, cancel the test.
        if ((WriteTest) && !WriteTest->IsRunning)
        {
            Test.IsCancelled = TRUE;
            break;
        }

		if (!Test.IsCancelled)
		{
			// Print benchmark stats
			if (ReadTest)
				ShowRunningStatus(ReadTest);
			else
				ShowRunningStatus(WriteTest);
		}
    }

	CONMSG("waiting for transfer thread(s)..\n");
	do
	{
		if (Mpl_Sem_TryWait(&g_ThreadBarrier) != MPL_SUCCESS)
		{
			MPL_SleepMs(10);
		} else {
			threadCount--;
		}
	}while(threadCount > 0);
	CONMSG("thread shutdown completed successfully..\n");

	/*
	// Wait for the transfer threads to complete gracefully if it
	// can be done in 10ms. All of the code from this point to
	// WaitForTestTransfer() is not required.  It is here only to
	// improve response time when the test is cancelled.
	//
    MPL_SleepMs(10);

	// If the thread is still running, abort and reset the endpoint.
    if ((ReadTest) && ReadTest->IsRunning)
        usb_resetep(Test.DeviceHandle, ReadTest->Ep.bEndpointAddress);

    // If the thread is still running, abort and reset the endpoint.
    if ((WriteTest) && WriteTest->IsRunning)
        usb_resetep(Test.DeviceHandle, WriteTest->Ep.bEndpointAddress);

    // Small delay incase usb_resetep() was called.
    MPL_SleepMs(10);

    // WaitForTestTransfer will not return until the thread
	// has exited.
    WaitForTestTransfer(ReadTest);
    WaitForTestTransfer(WriteTest);
	
	*/

    // Print benchmark detailed stats
	ShowTestInfo(&Test);
	if (ReadTest) ShowTransferInfo(ReadTest);
	if (WriteTest) ShowTransferInfo(WriteTest);


Done:
    if (Test.DeviceHandle)
    {
        usb_close(Test.DeviceHandle);
        Test.DeviceHandle = NULL;
    }
	if (Test.VerifyBuffer)
	{
		free(Test.VerifyBuffer);
		Test.VerifyBuffer = NULL;

	}
    FreeTransferParam(&ReadTest);
    FreeTransferParam(&WriteTest);

    CONMSG("\n[Press any key to exit]\n");
    ConIO_GetCh();
    CONMSG("\n");

	// Enable keyboard input echo on linux/mac.
	ConIO_EchoInput_Enabled();

	// Free UsbM
	usb_exit();

	// Free Mpl
	Mpl_Mutex_Free(&g_DisplayMutex);
	if (g_ThreadBarrier.Common.Valid == MPT_VALID)
		Mpl_Sem_Free(&g_ThreadBarrier);
	Mpl_Free();


    return 0;
}

//////////////////////////////////////////////////////////////////////////////
/* END OF PROGRAM                                                           */
//////////////////////////////////////////////////////////////////////////////
void ShowHelp(void)
{
	printf("\n");
	printf("USAGE: benchmark [list]\n");
	printf("                 [pid=] [vid=] [ep=] [intf=] [altf=]\n");
	printf("                 [read|write|loop] [notestselect]\n");
	printf("                 [verify|verifydetail]\n");
	printf("                 [retry=] [timeout=] [refresh=] [priority=]\n");
	printf("                 [mode=] [buffersize=] [buffercount=] [packetsize=]\n");
	printf("                 \n");
	printf("Commands:\n");
	printf("         list  : Display a list of connected devices before starting. \n");
	printf("                 Select the device to use for the test from the list.\n");
	printf("         read  : Read from the device.\n");
	printf("         write : Write to the device.\n");
	printf("         loop  : [Default] Read and write to the device at the same time.\n");
	printf("\n");
	printf("         notestselect : Skips submitting the control transfers to get/set the\n");
	printf("                        test type.  This makes the application compatible\n");
	printf("                        with non-benchmark firmwared. Use at your own risk!\n");
	printf("\n");
	printf("         verify       : Verify received data for loop and read tests. Report\n");
	printf("                        basic information on data validation errors.\n");
	printf("         verifydetail : Same as verify except reports detail information for \n");
	printf("                        each byte that fails validation.\n");
	printf("                        \n");
	printf("Switches:\n");
	printf("         vid        : Vendor id of device. (hex)  (Default=0x0666)\n");
	printf("         pid        : Product id of device. (hex) (Default=0x0001)\n");
	printf("         retry      : Number of times to retry a transfer that timeout.\n");
	printf("                      (Default = 0)\n");
	printf("         timeout    : Transfer timeout value. (milliseconds) (Default=5000)\n");
	printf("                      The timeout value used for read/write operations. If a\n");
	printf("                      transfer times out more than {retry} times, the test \n");
	printf("                      fails and the operation is aborted.\n");
	printf("         mode       : Sync|Async (Default=Sync) \n");
	printf("                      Sync uses the libusb-win32 sync transfer functions.\n");
	printf("                      Async uses the libusb-win32 asynchronous api.\n");
	printf("         buffersize : Transfer test size in bytes. (Default=4096)\n");
	printf("                      Increasing this value will generally yield higher\n");
	printf("                      transfer rates.\n");
	printf("         buffercount: (Async mode only) Number of outstanding transfers on\n");
	printf("                      an endpoint (Default=1, Max=10). Increasing this value\n");
	printf("                      will generally yield higher transfer rates.\n");
	printf("         refresh    : The display refresh interval. (in milliseconds)\n");
	printf("                      (Default=1000) This also effect the running status.\n");
	printf("         priority   : AboveNormal|BelowNormal|Highest|Lowest|Normal\n");
	printf("                      (Default=Normal) The thread priority level to use\n");
	printf("                      for the test.\n");
	printf("         ep         : The loopback endpoint to use. For example ep=0x01, would\n");
	printf("                      read from 0x81 and write to 0x01. (default is to use the\n");
	printf("                      (first read/write endpoint(s) in the interface)\n");
	printf("         intf       : The interface id the read/write endpoints reside in.\n");
	printf("         altf       : The alt interface id the read/write endpoints reside in.\n");
	printf("         packetsize : For isochronous use only. Sets the iso packet size.\n");
	printf("                      If not specified, the endpoints maximum packet size\n");
	printf("                      is used.         \n");
	printf("WARNING:\n");
	printf("          This program should only be used with USB devices which implement\n");
	printf("          one more more \"Benchmark\" interface(s).  Using this application\n");
	printf("          with a USB device it was not designed for can result in permanent\n");
	printf("          damage to the device.\n");
	printf("          \n");
	printf("Examples:\n");
	printf("\n");
	printf("benchmark vid=0x0666 pid=0x0001\n");
	printf("benchmark vid=0x4D2 pid=0x162E\n");
	printf("benchmark vid=0x4D2 pid=0x162E buffersize=65536\n");
	printf("benchmark read vid=0x4D2 pid=0x162E\n");
	printf("benchmark vid=0x4D2 pid=0x162E buffercount=3 buffersize=0x2000\n");
	printf("\n");
}

void ShowCopyright(void)
{
	CONMSG("libusb0(M) USB Benchmark\n");
	CONMSG("Copyright (c) 2012 Travis Robinson. <libusbdotnet@gmail.com>\n");
	CONMSG("http://sourceforge.net/projects/libusb-win32\n");
}
