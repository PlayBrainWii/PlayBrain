/* main.c
 *   by Alex Chadwick
 * 
 * Copyright (C) 2014, Alex Chadwick
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

typedef signed char s8;
typedef signed short s16;
typedef signed int s32;
typedef signed long long s64;

#include "../netslug_common.h"

#include <bslug.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rvl/GXGeometry.h>
#include <rvl/ipc.h>
#include <rvl/OSThread.h>
#include <rvl/vi.h>
#include <rvl/WPAD.h>
#include <smn/Dance.h>
#include <smn/RNG.h>

#include "network.h"

/* Which game to support. ? is a wild card (e.g. RMC? is any version of Mario
 * Kart Wii) */
BSLUG_MODULE_GAME("????");
BSLUG_MODULE_NAME("NetPlay");
BSLUG_MODULE_VERSION("dev");
BSLUG_MODULE_AUTHOR("MrBean35000vr and Chadderz");
/* quick guide to licenses:
 * BSD - Open source, anyone can use code for any purpose, even commercial.
 * GPL - Open source, anyone can use code but only in open source projects.
 * Freeware - Closed source, must be distributed for no monetary cost.
 * Proprietary - Closed source, may cost money, may be limited to individuals.
 * For more info search online!
 */
BSLUG_MODULE_LICENSE("BSD");

int host = -1;
int communicationSock = -1;

BSLUG_EXPORT(host);
BSLUG_EXPORT(communicationSock);

static bool MyOSCreateThread(
    OSThread_t *thread, OSThreadEntry_t entry_point, void *argument,
    void *stack_base, size_t stack_size, int priority, bool detached);
static void* sendThread_main(void *arg);
static void* recvThread_main(void *arg);
static void* controllerThread_main(void *arg);
static void MyWPADRead(int wiiremote, WPADData_t *data);
static void MyWPADInit(void);
static void WaitForNextFrame(void);
static ios_fd_t MyIOS_Open(const char *filepath, ios_mode_t mode);
static ios_ret_t MyIOS_IoctlAsync(
    ios_fd_t fd, int ioctl, const void *input, size_t input_length,
    void *output, size_t output_length, ios_cb_t cb, usr_t usrdata);
static WPADConnectCallback_t MyWPADSetConnectCallback(int wiimote, WPADConnectCallback_t newCallback);
static WPADExtensionCallback_t MyWPADSetExtensionCallback(int wiimote, WPADExtensionCallback_t newCallback);
static WPADSamplingCallback_t MyWPADSetSamplingCallback(int wiimote, WPADSamplingCallback_t newCallback);
static void Console_Write(const char *s) { }
static void My__VIRetraceHandler(int isr, void *context);
static int MyWPADSetDataFormat(int wiimote, WPADDataFormat_t format);
static WPADDataFormat_t MyWPADGetDataFormat(int wiimote);
static WPADStatus_t MyWPADProbe(int wiimote, WPADExtension_t *extension);
static void MyWPADSetAutoSamplingBuf(int wiimote, void *buffer, int count);
static int MyWPADGetLatestIndexInBuf(int wiimote);
static void MyWPADGetAccGravityUnit(int wiimote, WPADExtension_t extension, WPADAccGravityUnit_t *result);
static int MyWPADControlDpd(int wiimote, int command, WPADControlDpdCallback_t callback);
static bool MyWPADIsDpdEnabled(int wiimote);
static unsigned int MyRNG_Rand(rng_t *rng, unsigned int max);
static float MyRNG_RandFloat(rng_t *rng);
static float MyRNG_RandFloatZeroed(rng_t *rng);
static unsigned char MySMNDance_GetUnkown_a6c9(void);
static bool MySMNDance_GetUnkown_a6c9Mask(unsigned char mask);
static unsigned char MySMNDance_GetMask(void);
static bool MySMNDance_Enabled(unsigned char mask);
static unsigned char MySMNDance_GetUnkown_a6cb(void);
static int Myrand(void);

BSLUG_MUST_REPLACE(OSCreateThread, MyOSCreateThread);
BSLUG_MUST_REPLACE(WPADRead, MyWPADRead);
BSLUG_MUST_REPLACE(WPADInit, MyWPADInit);
BSLUG_MUST_REPLACE(WPADSetConnectCallback, MyWPADSetConnectCallback);
BSLUG_MUST_REPLACE(WPADSetExtensionCallback, MyWPADSetExtensionCallback);
BSLUG_REPLACE(WPADSetSamplingCallback, MyWPADSetSamplingCallback);
BSLUG_REPLACE(WPADSetAutoSamplingBuf, MyWPADSetAutoSamplingBuf);
BSLUG_REPLACE(WPADGetLatestIndexInBuf, MyWPADGetLatestIndexInBuf);
BSLUG_MUST_REPLACE(IOS_Open, MyIOS_Open);
BSLUG_MUST_REPLACE(IOS_IoctlAsync, MyIOS_IoctlAsync);
BSLUG_MUST_REPLACE(__VIRetraceHandler, My__VIRetraceHandler);
BSLUG_MUST_REPLACE(WPADSetDataFormat, MyWPADSetDataFormat);
BSLUG_REPLACE(WPADGetDataFormat, MyWPADGetDataFormat);
BSLUG_MUST_REPLACE(WPADProbe, MyWPADProbe);
BSLUG_MUST_REPLACE(WPADGetAccGravityUnit, MyWPADGetAccGravityUnit);
BSLUG_MUST_REPLACE(WPADControlDpd, MyWPADControlDpd);
BSLUG_MUST_REPLACE(WPADIsDpdEnabled, MyWPADIsDpdEnabled);
BSLUG_REPLACE(RNG_Rand, MyRNG_Rand);
BSLUG_REPLACE(RNG_RandFloat, MyRNG_RandFloat);
BSLUG_REPLACE(RNG_RandFloatZeroed, MyRNG_RandFloatZeroed);
BSLUG_REPLACE(SMNDance_GetUnkown_a6c9, MySMNDance_GetUnkown_a6c9);
BSLUG_REPLACE(SMNDance_GetUnkown_a6c9Mask, MySMNDance_GetUnkown_a6c9Mask);
BSLUG_REPLACE(SMNDance_GetMask, MySMNDance_GetMask);
BSLUG_REPLACE(SMNDance_Enabled, MySMNDance_Enabled);
BSLUG_REPLACE(SMNDance_GetUnkown_a6cb, MySMNDance_GetUnkown_a6cb);
BSLUG_REPLACE(rand, Myrand);

// Symbols for bslug_common.h.
#ifndef NDEBUG
static void Debug_CheckFrameAdvanceHandler(bool skip);
static int Debug_CheckVariable = 0;
frame_handler_t on_frame_advance = &Debug_CheckFrameAdvanceHandler;
#else
frame_handler_t on_frame_advance = 0;
#endif

int network_error = 0;

BSLUG_EXPORT(on_frame_advance);
BSLUG_EXPORT(network_error);

static bool createdThread = false;
static bool initCalled = false;
static bool framewaitqueueEnabled = false;
static WPADConnectCallback_t connectCallback[4];
static WPADExtensionCallback_t extensionCallback[4];
static WPADSamplingCallback_t samplingCallback[4];
static void *autoSamplingBuffer[4];
static int autoSamplingBufferCount[4];
static int autoSamplingBufferIndex[4];

extern int net_ip_top_fd;
static OSThread_t sendThread;
static OSThread_t recvThread;
static OSThread_t controllerThread;
static char sendThreadStack[0x4000];
static char recvThreadStack[0x4000];
static char controllerThreadStack[0x4000];
static bool canaried;

static OSThreadQueue_t framewaitqueue = { };
static OSThreadQueue_t wpadReadQueue = { };
static OSThreadQueue_t hangGameQueue = { };
static OSThreadQueue_t IoctlBufferThreadQueue = { };
static OSThreadQueue_t SendBufferThreadQueue = { };

static ios_fd_t discFD = -1;

static int frameCount = 0;

static WPADDataFormat_t currentFormat[4];

/**
 * Structure representing a node the FST file tree used by Wii games.
 */
typedef struct fst_t {
  struct {
    unsigned type : 8;
    unsigned name : 24;
  };
  union {
    u32 fileOffset;
    u32 parentOffset;
  };
  union {
    u32 fileLen;
    u32 nextOffset; // next in the tree as if node had no children
  };
} __attribute__((packed)) fst;


static const fst * const * const game_fst = (const fst * const * const)0x80000038;

// Packet sent from guests to host.
struct sendPacket {
	int sent; // 0 = invalid, 1 = to be sent, 2 = sent succesfully (may be reused)
	int type; // 0 = controller, 1 = di_read
	int clientNumber; // 0 is host
	union 
	{
		struct
		{
			int frameNumberToDeliverOn;
			int wiimote;
			WPADDataFormat_t format;
			WPADStatus_t status;
			WPADExtension_t extension;
			WPADData_t inputs;
			WPADAccGravityUnit_t gravityUnit[2];
		} controller;
		struct
		{
			unsigned int sector;
		} di_read;
	} data;
};

// Packet sent from host to guests.
struct ctrlPacket {
	int frameNumber;
	long long frameSeed;
	bool haveSentOrRecv;
	bool haveInput[4];
	WPADDataFormat_t formats[4];
	WPADStatus_t status[4];
	WPADExtension_t extension[4];
	WPADData_t inputs[4];
	WPADAccGravityUnit_t gravityUnit[4][2];
	unsigned int callback_sector;
	union
	{
		struct
		{
			unsigned char dance_a6c9;
			unsigned char dance_a6ca;
			unsigned char dance_a6cb;
		} SMN;
	} game;
};

#define IOCTL_BUFFER_SIZE 20

#define MAX_PLAYERS 2
#define NUMBER_OF_INPUTS_TO_WAIT_FOR 2
#define HOSTS_WIIMOTE_NUMBER 0
#define CLIENTS_WIIMOTE_NUMBER 1

static struct IoctlManager {
	int state; // 0 = Invalid (callback used), 1 = Called ioctlasync, 2 = received real callback
	unsigned int sector;
	usr_t userData;
	ios_cb_t callback;
	bool haveCallback[MAX_PLAYERS];
	bool haveSent;
} IoctlBuffer [IOCTL_BUFFER_SIZE];

static void ourCallback(ios_ret_t result, usr_t usrdata);

// ******************************************************************************************************
#define BUFFER_SIZE 5

// Produced by controller thread on host & guest
// Consumed by send thread on host & guest
//  - On guest this is genuinely sent over network.
//  - On host this is just copied to ctrlBuffer.
static struct sendPacket sendBuffer[BUFFER_SIZE];
// Produced by send & recv thread on host
//  - recv fills in any inputs needed from other machine
//  - when they've all arrived send thread copies local input from sendBuffer and broadcasts the ctrlPacket to clients
// Produced by recv thread on guest
// Consumed by callbackThread on host & guest
static struct ctrlPacket ctrlBuffer[BUFFER_SIZE];

#define _CPU_ISR_Disable( _isr_cookie ) \
  { register u32 _disable_mask = 0; \
	_isr_cookie = 0; \
    __asm__ __volatile__ ( \
	  "mfmsr %0\n" \
	  "rlwinm %1,%0,0,17,15\n" \
	  "mtmsr %1\n" \
	  "extrwi %0,%0,1,16" \
	  : "=&r" ((_isr_cookie)), "=&r" ((_disable_mask)) \
	  : "0" ((_isr_cookie)), "1" ((_disable_mask)) \
	); \
  }

#define _CPU_ISR_Restore( _isr_cookie )  \
  { register u32 _enable_mask = 0; \
	__asm__ __volatile__ ( \
    "    cmpwi %0,0\n" \
	"    beq 1f\n" \
	"    mfmsr %1\n" \
	"    ori %1,%1,0x8000\n" \
	"    mtmsr %1\n" \
	"1:" \
	: "=r"((_isr_cookie)),"=&r" ((_enable_mask)) \
	: "0"((_isr_cookie)),"1" ((_enable_mask)) \
	); \
  }

#define _CPU_FP_Enable( _isr_cookie ) \
  { register u32 _disable_mask = 0; \
	_isr_cookie = 0; \
    __asm__ __volatile__ ( \
	  "mfmsr %0\n" \
	  "ori %1,%0,0x2000\n" \
	  "mtmsr %1\n" \
	  "extrwi %0,%0,1,18" \
	  : "=&r" ((_isr_cookie)), "=&r" ((_disable_mask)) \
	  : "0" ((_isr_cookie)), "1" ((_disable_mask)) \
	); \
  }

#define _CPU_FP_Restore( _isr_cookie )  \
  { register u32 _enable_mask = 0; \
	__asm__ __volatile__ ( \
    "    cmpwi %0,0\n" \
	"    bne 1f\n" \
	"    mfmsr %1\n" \
	"    rlwinm %1,%1,0,19,17\n" \
	"    mtmsr %1\n" \
	"1:" \
	: "=r"((_isr_cookie)),"=&r" ((_enable_mask)) \
	: "0"((_isr_cookie)),"1" ((_enable_mask)) \
	); \
  }

#define mftbl() ({register u32 _rval; \
		asm volatile("mftbl %0" : "=r"(_rval)); _rval;})
#define mflr() ({register u32 _rval; \
		asm volatile("mflr %0" : "=r"(_rval)); _rval;})
#define mfsp() ({register u32 _rval; \
		asm volatile("mr %0,1" : "=r"(_rval)); _rval;})


static inline void printInt(int value)
{
	static char * c[] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c", "d", "e", "f" };
	Console_Write("0x");
	for (int i = 7; i >= 0; i--)
	{
		Console_Write(c[(value >> (i * 4)) & 0xf]);
	}
}

#define TB_BUS_CLOCK				243000000u
#define TB_CORE_CLOCK				729000000u


#define TB_TIMER_CLOCK				(TB_BUS_CLOCK/4000)			//4th of the bus frequency

#define TB_SECSPERMIN				60
#define TB_MINSPERHR				60
#define TB_MONSPERYR				12
#define TB_DAYSPERYR				365
#define TB_HRSPERDAY				24
#define TB_SECSPERDAY				(TB_SECSPERMIN*TB_MINSPERHR*TB_HRSPERDAY)
#define TB_SECSPERNYR				(365*TB_SECSPERDAY)
								
#define TB_MSPERSEC					1000
#define TB_USPERSEC					1000000
#define TB_NSPERSEC					1000000000
#define TB_NSPERMS					1000000
#define TB_NSPERUS					1000
#define TB_USPERTICK				10000

#define ticks_to_cycles(ticks)		((((u64)(ticks)*(u64)((TB_CORE_CLOCK*2)/TB_TIMER_CLOCK))/2))
#define ticks_to_secs(ticks)		(((u64)(ticks)/(u64)(TB_TIMER_CLOCK*1000)))
#define ticks_to_millisecs(ticks)	(((u64)(ticks)/(u64)(TB_TIMER_CLOCK)))
#define ticks_to_microsecs(ticks)	((((u64)(ticks)*8)/(u64)(TB_TIMER_CLOCK/125)))
#define ticks_to_nanosecs(ticks)	((((u64)(ticks)*8000)/(u64)(TB_TIMER_CLOCK/125)))

#define tick_microsecs(ticks)		((((u64)(ticks)*8)%(u64)(TB_TIMER_CLOCK/125)))
#define tick_nanosecs(ticks)		((((u64)(ticks)*8000)%(u64)(TB_TIMER_CLOCK/125)))


#define secs_to_ticks(sec)			((u64)(sec)*(TB_TIMER_CLOCK*1000))
#define millisecs_to_ticks(msec)	((u64)(msec)*(TB_TIMER_CLOCK))
#define microsecs_to_ticks(usec)	(((u64)(usec)*(TB_TIMER_CLOCK/125))/8)
#define nanosecs_to_ticks(nsec)		(((u64)(nsec)*(TB_TIMER_CLOCK/125))/8000)


static int ioctlsSent = 0;
static int ioctlsRecv = 0;

static int IoctlBufferAllocator(unsigned int sector)
{
	int isr;
	_CPU_ISR_Disable(isr);

	bool hang = false;
	while (true)
	{
		int i;
		// Look for this sector already in the buffer
		for (i = 0; i < IOCTL_BUFFER_SIZE; i++)
		{
			if (IoctlBuffer[i].state >= 1 && IoctlBuffer[i].sector == sector)
			{
				break;
			}
		}
		
		if (i == IOCTL_BUFFER_SIZE)
		{
			// Allocate a new sector
			for (i = 0; i < IOCTL_BUFFER_SIZE; i++)
			{
				if (IoctlBuffer[i].state == 0)
				{
					break;
				}
			}
		}

		if (i == IOCTL_BUFFER_SIZE)
		{
			// Buffer full!
			if (!hang)
			{
				Console_Write("[IOCTL] Hung on full IoctlBuffer.\n");
				hang = true;
			}
			OSSleepThread(&IoctlBufferThreadQueue);
			continue;
		}

		if (IoctlBuffer[i].state == 0)
		{
			IoctlBuffer[i].state = 1;
			IoctlBuffer[i].sector = sector;
			for (int j = 0; j < MAX_PLAYERS; j++)
				IoctlBuffer[i].haveCallback[j] = false;
			IoctlBuffer[i].haveSent = false;
		}
		
		_CPU_ISR_Restore(isr);
		return i;
	}
}

// On host, uses packet for receivey logicy thingy to populate controllBuffer.. yeah, that's right
// On client sends message to host (via sendBuffer & sendThread)
static void ProcessPacket(struct sendPacket *pkt)
{
	int isr;
	_CPU_ISR_Disable(isr);
	if (pkt->type == 0) // Controller data
	{
		int fntdo = pkt->data.controller.frameNumberToDeliverOn;
		int index = fntdo % BUFFER_SIZE;

		while (ctrlBuffer[index].frameNumber != fntdo)
		{
			WaitForNextFrame();
		}
		ctrlBuffer[index].formats[pkt->data.controller.wiimote] = pkt->data.controller.format;
		ctrlBuffer[index].status[pkt->data.controller.wiimote] = pkt->data.controller.status;
		ctrlBuffer[index].extension[pkt->data.controller.wiimote] = pkt->data.controller.extension;
		ctrlBuffer[index].inputs[pkt->data.controller.wiimote] = pkt->data.controller.inputs;
		ctrlBuffer[index].gravityUnit[pkt->data.controller.wiimote][0] = pkt->data.controller.gravityUnit[0];
		ctrlBuffer[index].gravityUnit[pkt->data.controller.wiimote][1] = pkt->data.controller.gravityUnit[1];
		ctrlBuffer[index].haveInput[pkt->data.controller.wiimote] = true;
	}
	if (host)
	{
		if (pkt->type == 0) // Controller data
		{
			// Already done above
		}
		else if (pkt->type == 1)
		{
			int i = IoctlBufferAllocator(pkt->data.di_read.sector);
			IoctlBuffer[i].haveCallback[pkt->clientNumber] = true;
		}
		else
		{
			Console_Write("[PKT] Unknown packet received!\n");
		}
	}
	else
	{
		static int sendBufferPos = 0;
		
		while (sendBuffer[sendBufferPos].sent == 1)
		{
			OSSleepThread(&SendBufferThreadQueue);
		}

		sendBuffer[sendBufferPos] = *pkt;
		sendBuffer[sendBufferPos].sent = 1;
		OSWakeupThread(&SendBufferThreadQueue);
		//Console_Write("[PKT] Sent a packet.\n");

		sendBufferPos++;
		if (sendBufferPos >= BUFFER_SIZE)
		{
			sendBufferPos = 0;
		}
	}
	_CPU_ISR_Restore(isr);
}

static ios_fd_t MyIOS_Open(const char *filepath, ios_mode_t mode)
{
	if (!strcmp(filepath, "/dev/di"))
	{
		discFD = IOS_Open(filepath, mode);
		return discFD;
	}	

	return IOS_Open(filepath, mode);
}

static inline long long gettime()
{
	unsigned long low, high;
	__asm__ __volatile__ (
		"1:	mftbu	%1\n\
		    mftb	%0\n\
		    mftbu	5\n\
			cmpw	%1,5\n\
			bne		1b"
			: "=r"(low), "=r"(high) : : "5");
	return ((long long)high << 32) | low;
}

static ios_ret_t MyIOS_IoctlAsync(
    ios_fd_t fd, int ioctl, const void *input, size_t input_length,
    void *output, size_t output_length, ios_cb_t cb, usr_t usrdata)
{

	static int skipCount = 0;
	bool doIntercept = false;
	const char *name = "VOID";
	static int fileAccessCounts[4096];
	int fileNumber = 4095;

	if (fd == discFD && (ioctl == 141 || ioctl == 113))
	{
		/* valid ioctl values for /dev/di
		112 = DVDLowReadDiskID
		113 = DVDLowRead
		136 = DVDLowGetCoverStatus 
		138 = DVDLowReset
		140 = DVDLowClosePartition
		141 = DVDLowUnecryptedRead
		224 = DVDLowRequestError
		227 = DVDLowStopMotor
		*/

		doIntercept = true;
		const fst * const the_fst = *game_fst;
		const int node_count = the_fst[0].nextOffset;
		const char * const names = (const char *)(the_fst + node_count);
		u32 offset = ((u32 *)input)[2];
		int i;
		for (i = 0; i < node_count; i++)
		{
			if (the_fst[i].type == 0 && the_fst[i].fileOffset <= offset && the_fst[i].fileOffset + (the_fst[i].fileLen >> 2) > offset)
			{
				// We've found a file for this read!
				name = names + the_fst[i].name;
				fileNumber = i;
				size_t len = strlen(name);

				if (len > 6 && strcmp(name + len - 6, ".brstm") == 0 && memcmp(name, "STRM_BGM_STAR", 13) != 0)
				{
					/*Console_Write("[IOCTL] Caught brstm: ");
					Console_Write(name);
					Console_Write(".\n");*/
					doIntercept = offset - the_fst[i].fileOffset < 0x10;
				}
				else if (len > 4 && strcmp(name + len - 4, ".thp") == 0)
				{
					/*Console_Write("[IOCTL] Caught thp: ");
					Console_Write(name);
					Console_Write(".\n");*/
					doIntercept = offset - the_fst[i].fileOffset < 0x10;
				}
				break;
			}
		}
		if (i == node_count)
		{
			Console_Write("[IOCTL] Couldn't place a read in a file, sector: ");
			printInt(offset);
			Console_Write(".\n");
		}
	}
	else
	{
		doIntercept = false;
	}

	if (doIntercept && ++skipCount < 40)
	{
		doIntercept = false;
	}

	if (doIntercept)
	{
		int isr;
		_CPU_ISR_Disable(isr);
		if (skipCount == 40)
		{
			Console_Write("[IOCTL] Sync enabled!\n");
		}
		
		if (fileNumber > sizeof(fileAccessCounts) / sizeof(fileAccessCounts[0]))
		{
			Console_Write("[IOCTL] That's more files than I can count!\n");
			fileNumber = sizeof(fileAccessCounts) / sizeof(fileAccessCounts[0]) - 1;
		}
		fileAccessCounts[fileNumber]++;
		unsigned int sector = (fileAccessCounts[fileNumber] << 12) | fileNumber;
		int IoctlBufferPos = IoctlBufferAllocator(sector);

		IoctlBuffer[IoctlBufferPos].userData = usrdata;
		IoctlBuffer[IoctlBufferPos].callback = cb;

		ios_ret_t IoctlRet = IOS_IoctlAsync(fd, ioctl, input, input_length, output, output_length, ourCallback, (void*)IoctlBufferPos);

		if (IoctlRet < 0)
		{
			// if the other Wii didn't fail here, we're boned. If it did, all is well.
			Console_Write("[IOCTL] IOS_IoctlAsync Failed! This is fatal because of a race condition.\n");
			IoctlBuffer[IoctlBufferPos].state = 0;
			_CPU_ISR_Restore(isr);
			return IoctlRet;
		}
		
		struct sendPacket pkt;
		pkt.type = 1;
		pkt.clientNumber = host ? 0 : 1;
		pkt.data.di_read.sector = sector;
		
		Console_Write("[IOCTL] ");
		printInt(frameCount);
		Console_Write(" ");
		printInt(((int *)input)[1]);
		Console_Write(" ");
		printInt(((int *)input)[2]);
		Console_Write(" ");
		printInt(sector);
		Console_Write(" ");
		Console_Write(name);
		Console_Write("\n");
		ioctlsSent++;

		OSWakeupThread(&IoctlBufferThreadQueue);
		_CPU_ISR_Restore(isr);
		
		ProcessPacket(&pkt);
		return IoctlRet;
	}
	else
	{
		return IOS_IoctlAsync(fd, ioctl, input, input_length, output, output_length, cb, usrdata);
	}
}

static void ourCallback(ios_ret_t result, usr_t usrdata)
{
	int IoctlBufferPos = (int)usrdata;
	if (result != 1)
		Console_Write("[DRE] DRE!\n");
	IoctlBuffer[IoctlBufferPos].state = 2;
	OSWakeupThread(&IoctlBufferThreadQueue);
}

static void WAKEUP(void)
{
	if (framewaitqueueEnabled)
	{
		OSWakeupThread(&framewaitqueue);
	}
}

static void WaitForNextFrame(void)
{
	OSSleepThread(&framewaitqueue);
}

static bool MyOSCreateThread(
    OSThread_t *thread, OSThreadEntry_t entry_point, void *argument,
    void *stack_base, size_t stack_size, int priority, bool detached)
{

	if (!createdThread)
	{
		for (int i = 0; i < 4; i++)
		{
			ctrlBuffer[0].status[i] = WPAD_STATUS_DISCONNECTED;
		}
		OSInitThreadQueue(&framewaitqueue);
		OSInitThreadQueue(&wpadReadQueue);
		OSInitThreadQueue(&hangGameQueue);
		OSInitThreadQueue(&IoctlBufferThreadQueue);
		OSInitThreadQueue(&SendBufferThreadQueue);
		framewaitqueueEnabled = true;
		OSCreateThread(&sendThread, sendThread_main, 0, sendThreadStack + sizeof(sendThreadStack), sizeof(sendThreadStack), THREAD_PRIORITY_HIGHEST, false);
		OSResumeThread(&sendThread);
		OSCreateThread(&recvThread, recvThread_main, 0, recvThreadStack + sizeof(recvThreadStack), sizeof(recvThreadStack), THREAD_PRIORITY_HIGHEST, false);
		OSResumeThread(&recvThread);
		OSCreateThread(&controllerThread, controllerThread_main, 0, controllerThreadStack + sizeof(controllerThreadStack), sizeof(controllerThreadStack), THREAD_PRIORITY_HIGHEST, false);
		OSResumeThread(&controllerThread);
		for (int i = 0; i < 100; i++)
		{
			sendThreadStack[i] = 0xa5;
			recvThreadStack[i] = 0xa5;
			controllerThreadStack[i] = 0xa5;
		}
		canaried = true;
		createdThread = true;
	}

	return OSCreateThread(thread, entry_point, argument, stack_base, stack_size, priority, detached);
}

static int reliable_send(int socket, const void *data, int size) {
	for (int i = 0; i < size; ) {
		int amt = Mynet_send(socket, (const char *)data + i, size - i, 0);
		if (amt <= 0) return amt;
		i += amt;
	}
	return size;
}

static int reliable_recv(int socket, void *data, int size) {
	for (int i = 0; i < size; ) {
		int amt = Mynet_recv(socket, (char *)data + i, size - i, 0);
		if (amt <= 0) return amt;
		i += amt;
	}
	return size;
}

static void* sendThread_main(void *arg)
{

	Console_Write("[SEND] Thread started.\n");

	int sendBufferPos = 0;
	int ctrlBufferPos = 0;
	int nextFrameToSend = 0;
	long long frameSeed = 0;
	
	// Mark the first BUFFER_SIZE frames as ready (since they're, by definition, unused).
	for (int i = 0; i < BUFFER_SIZE; i++)
	{
		ctrlBuffer[i].frameNumber = i;
	}

	while (true)
	{
		if (host)
		{
			while (ctrlBuffer[ctrlBufferPos].frameNumber != nextFrameToSend)
			{
				WaitForNextFrame();
			}

			// Wait for all controllers to be available
			if (nextFrameToSend > BUFFER_SIZE)
			{
				while (!ctrlBuffer[ctrlBufferPos].haveInput[HOSTS_WIIMOTE_NUMBER])
				{
					WaitForNextFrame();
				}
			}
			int isr;
			_CPU_ISR_Disable(isr);
			// Disconnect ALL controllers until frame BUFFER_SIZE.
			// Then just disconnect the ones we aren't using.
			for (int i = nextFrameToSend > BUFFER_SIZE ? NUMBER_OF_INPUTS_TO_WAIT_FOR : 0; i < 4; i++)
			{
				ctrlBuffer[ctrlBufferPos].status[i] = WPAD_STATUS_DISCONNECTED;
				ctrlBuffer[ctrlBufferPos].haveInput[i] = true;
			}
			frameSeed = frameSeed * 1103515245 + 12345;
			ctrlBuffer[ctrlBufferPos].frameSeed = frameSeed;

			if (memcmp((char *)0x80000000, "SMN", 3) == 0) 
			{
				ctrlBuffer[ctrlBufferPos].game.SMN.dance_a6c9 = SMNDance_GetUnkown_a6c9();
				ctrlBuffer[ctrlBufferPos].game.SMN.dance_a6ca = SMNDance_GetMask();
				ctrlBuffer[ctrlBufferPos].game.SMN.dance_a6cb = SMNDance_GetUnkown_a6cb();
			}

			ctrlBuffer[ctrlBufferPos].callback_sector = 0;
			for (int i = 0; i < IOCTL_BUFFER_SIZE; i++)
			{
				if (IoctlBuffer[i].state >= 1 && IoctlBuffer[i].haveSent == false)
				{
					int j;
					for (j = 0; j < 2; j++)
					{
						if (!IoctlBuffer[i].haveCallback[j]) break;
					}
					if (j == 2)
					{
						// All have called back!
						IoctlBuffer[i].haveSent = true;
						ctrlBuffer[ctrlBufferPos].callback_sector = IoctlBuffer[i].sector;
					}
				}
			}

			_CPU_ISR_Restore(isr);

			while(reliable_send(communicationSock, &ctrlBuffer[ctrlBufferPos], sizeof(ctrlBuffer[ctrlBufferPos])) <= 0)
			{
				Console_Write("[SEND] Failure. OHHHHHH.\n");
				network_error = 1;
			}

			ctrlBuffer[ctrlBufferPos].haveSentOrRecv = true;
				
			OSWakeupThread(&wpadReadQueue); // New data for WPADRead

			ctrlBufferPos++;
			if (ctrlBufferPos >= BUFFER_SIZE)
			{
				ctrlBufferPos = 0;
			}
			nextFrameToSend++;
		}
		else // client
		{
			while (sendBuffer[sendBufferPos].sent != 1)
			{
				int isr;
				_CPU_ISR_Disable(isr);
				if (sendBuffer[sendBufferPos].sent != 1)
				{
					OSSleepThread(&SendBufferThreadQueue);
				}
				_CPU_ISR_Restore(isr);
			}

			while(reliable_send(communicationSock, &sendBuffer[sendBufferPos], sizeof(sendBuffer[sendBufferPos])) <= 0)
			{
				Console_Write("[SEND] Failure. OHHHHHH.\n");
				network_error = 1;
			}
			
			sendBuffer[sendBufferPos].sent = 2;
			OSWakeupThread(&SendBufferThreadQueue);

			sendBufferPos++;
			if (sendBufferPos >= BUFFER_SIZE)
			{
				sendBufferPos = 0;
			}
		}
	}

	return 0;
}

static void* recvThread_main(void *arg)
{
	Console_Write("[RECV] Thread started.\n");

	int ctrlBufferPos = 0;

	while (true)
	{
		if (host)
		{
			struct sendPacket inPacket;
			
			while(reliable_recv(communicationSock, &inPacket, sizeof(inPacket)) <= 0)
			{
				Console_Write("[RECV] Failure. OHHHHHH.\n");
				network_error = 1;
			}

			ProcessPacket(&inPacket);
		}
		else
		{
			struct ctrlPacket inPacket;
			
			while(reliable_recv(communicationSock, &inPacket, sizeof(inPacket)) <= 0)
			{
				Console_Write("[RECV] Failure. OHHHHHH.\n");
				network_error = 1;
			}

			if (ctrlBuffer[ctrlBufferPos].frameNumber != 0)
			{
				while (ctrlBuffer[ctrlBufferPos].frameNumber != inPacket.frameNumber)
				{
					WaitForNextFrame();
				}
			}

			int isr;
			_CPU_ISR_Disable(isr);
			for (int i = 0; i < 4; i++) {
				if (!inPacket.haveInput[i] && ctrlBuffer[ctrlBufferPos].haveInput[i])
				{
					inPacket.haveInput[i] = ctrlBuffer[ctrlBufferPos].haveInput[i];
					inPacket.formats[i] = ctrlBuffer[ctrlBufferPos].formats[i];
					inPacket.status[i] = ctrlBuffer[ctrlBufferPos].status[i];
					inPacket.extension[i] = ctrlBuffer[ctrlBufferPos].extension[i];
					inPacket.inputs[i] = ctrlBuffer[ctrlBufferPos].inputs[i];
					inPacket.gravityUnit[i][0] = ctrlBuffer[ctrlBufferPos].gravityUnit[i][0];
					inPacket.gravityUnit[i][1] = ctrlBuffer[ctrlBufferPos].gravityUnit[i][1];
				}
			}
			ctrlBuffer[ctrlBufferPos] = inPacket;
			ctrlBuffer[ctrlBufferPos].haveSentOrRecv = true;
			_CPU_ISR_Restore(isr);
			
			OSWakeupThread(&wpadReadQueue); // New data for WPADRead

			ctrlBufferPos++;
			if (ctrlBufferPos >= BUFFER_SIZE)
			{
				ctrlBufferPos = 0;
			}
		}
	}

	return 0;
}

static void* controllerThread_main(void* arg)
{
	Console_Write("[CTRL] Thread started.\n");
	// The host skips the first BUFFER_SIZE inputs.
	unsigned int nextFrameToSend = BUFFER_SIZE;

	while (true)
	{
		while (frameCount + BUFFER_SIZE <= nextFrameToSend + 1)
		{
			WaitForNextFrame();
		}
		
		struct sendPacket pkt;
		pkt.type = 0;
		pkt.clientNumber = host ? 0 : 1;
		pkt.data.controller.frameNumberToDeliverOn = nextFrameToSend;
		pkt.data.controller.wiimote = host ? HOSTS_WIIMOTE_NUMBER : CLIENTS_WIIMOTE_NUMBER;
		if (initCalled)
		{
			WPADRead(0, &pkt.data.controller.inputs);
			pkt.data.controller.format = WPADGetDataFormat(0);
			pkt.data.controller.status = WPADProbe(0, &pkt.data.controller.extension);
			WPADGetAccGravityUnit(0, WPAD_EXTENSION_NONE, &pkt.data.controller.gravityUnit[0]);
			WPADGetAccGravityUnit(0, WPAD_EXTENSION_NUNCHUCK, &pkt.data.controller.gravityUnit[1]);
		}
		else
		{
			pkt.data.controller.status = WPAD_STATUS_DISCONNECTED;
		}

		ProcessPacket(&pkt);
		nextFrameToSend++;
	}

	return 0;
}

static void MyWPADRead(int wiiremote, WPADData_t *data)
{
	int isr;
	_CPU_ISR_Disable(isr);
	int fCnt = frameCount;
	int index = fCnt % BUFFER_SIZE;
	
	memset(data, 0, WPADDataFormatSize(currentFormat[wiiremote]));
	if (ctrlBuffer[index].formats[wiiremote] == currentFormat[wiiremote])
	{
		memcpy(data, &ctrlBuffer[index].inputs[wiiremote], WPADDataFormatSize(currentFormat[wiiremote]));
	}
	else
	{
		// Copy the fields common to all formats.
		memcpy(data, &ctrlBuffer[index].inputs[wiiremote], WPADDataFormatSize(WPAD_FORMAT_NONE));
	}
	_CPU_ISR_Restore(isr);
}

static WPADStatus_t MyWPADProbe(int wiimote, WPADExtension_t *extension)
{
	int isr;
	_CPU_ISR_Disable(isr);
	int fCnt = frameCount;
	int index = fCnt % BUFFER_SIZE;
	WPADStatus_t status;
	
	status = ctrlBuffer[index].status[wiimote];
	if (extension != NULL)
	{
		*extension = ctrlBuffer[index].extension[wiimote];
	}
	_CPU_ISR_Restore(isr);
	return status;
}

static void MyWPADInit(void)
{	
	WPADInit();
	initCalled = true;
}

static WPADConnectCallback_t MyWPADSetConnectCallback(int wiimote, WPADConnectCallback_t newCallback)
{
	// remember their callback
	WPADConnectCallback_t old = connectCallback[wiimote];
	connectCallback[wiimote] = newCallback;
	return old;
}

static WPADExtensionCallback_t MyWPADSetExtensionCallback(int wiimote, WPADExtensionCallback_t newCallback)
{
	// remember their callback
	WPADExtensionCallback_t old = extensionCallback[wiimote];
	extensionCallback[wiimote] = newCallback;
	return old;
}

static WPADSamplingCallback_t MyWPADSetSamplingCallback(int wiimote, WPADSamplingCallback_t newCallback)
{
	// remember their callback
	WPADSamplingCallback_t old = samplingCallback[wiimote];
	samplingCallback[wiimote] = newCallback;
	return old;
}

static void MyWPADSetAutoSamplingBuf(int wiimote, void *buffer, int count)
{
	int isr;
	_CPU_ISR_Disable(isr);
	autoSamplingBuffer[wiimote] = buffer;
	autoSamplingBufferCount[wiimote] = count;
	_CPU_ISR_Restore(isr);
}

static int MyWPADGetLatestIndexInBuf(int wiimote)
{
	return autoSamplingBufferIndex[wiimote];
}

static void MyWPADGetAccGravityUnit(int wiimote, WPADExtension_t extension, WPADAccGravityUnit_t *result)
{
	int isr;
	_CPU_ISR_Disable(isr);
	int fCnt = frameCount;
	int index = fCnt % BUFFER_SIZE;
	if (extension == WPAD_EXTENSION_NONE)
	{
		*result = ctrlBuffer[index].gravityUnit[wiimote][0];
	}
	else if (extension == WPAD_EXTENSION_NUNCHUCK)
	{
		*result = ctrlBuffer[index].gravityUnit[wiimote][1];
	}
	else
	{
		result->acceleration[0] = 0;
		result->acceleration[1] = 0;
		result->acceleration[2] = 0;
	}
	_CPU_ISR_Restore(isr);
}

static void My__VIRetraceHandler(int isr, void *context)
{
	static int stillFrames = 0;
	int fp_isr;
	_CPU_FP_Enable(fp_isr);

	if (canaried)
	{
		for (int i = 0; i < 100; i++)
		{
			if (sendThreadStack[i] != 0xa5)
			{
				Console_Write("[CANARY] (send thread) *dies*\n");
				while (1);
			}
			if (recvThreadStack[i] != 0xa5)
			{
				Console_Write("[CANARY] (recv thread) *dies*\n");
				while (1);
			}
			if (controllerThreadStack[i] != 0xa5)
			{
				Console_Write("[CANARY] (ctrl thread) *dies*\n");
				while (1);
			}
		}
	}
	WAKEUP();

	int lastFrameIndex = frameCount % BUFFER_SIZE;
	int nextFrameIndex = (frameCount + 1) % BUFFER_SIZE;
	bool stall = createdThread && (ctrlBuffer[nextFrameIndex].frameNumber != frameCount + 1);
	int ioctlBufferIndex = -1;

	if (!stall)
	{
		if (!ctrlBuffer[nextFrameIndex].haveSentOrRecv)
		{
			stall = true;
		}
	}

	if (!stall)
	{
		for (int i = 0; i < NUMBER_OF_INPUTS_TO_WAIT_FOR; i++)
		{
			if (!ctrlBuffer[nextFrameIndex].haveInput[i])
			{
				stall = true;
				break;
			}
		}
	}

	if (!stall && createdThread && ctrlBuffer[nextFrameIndex].callback_sector != 0)
	{
		for (int i = 0; i < IOCTL_BUFFER_SIZE; i++)
		{
			if (IoctlBuffer[i].state == 1 && ctrlBuffer[nextFrameIndex].callback_sector == IoctlBuffer[i].sector)
			{
				// The callback we need hasn't happened.
				stall = true;
				break;
			}
			if (IoctlBuffer[i].state == 2 && ctrlBuffer[nextFrameIndex].callback_sector == IoctlBuffer[i].sector)
			{
				ioctlBufferIndex = i;
				break;
			}
		}
	}

	if (on_frame_advance != 0)
	{
#ifndef NDEBUG
		int check = Debug_CheckVariable;
#endif
		on_frame_advance(stall);
#ifndef NDEBUG
		if (check == Debug_CheckVariable)
		{
			Console_Write("[DECC] ERROR: on_frame_advance overwritten without chaining the handler. Read the comments people!\n");
		}
#endif
	}

	if (!stall)
	{
		stillFrames = 0;

		if (createdThread)
		{
			frameCount++;

			if (ioctlBufferIndex != -1)
			{
				Console_Write("[DECC] Callback ");
				printInt(frameCount);
				Console_Write(" ");
				printInt(ioctlsSent);
				Console_Write(" ");
				printInt(ioctlsRecv);
				Console_Write(" ");
				printInt(ctrlBuffer[nextFrameIndex].callback_sector);
				Console_Write("\n");

				IoctlBuffer[ioctlBufferIndex].callback(1, IoctlBuffer[ioctlBufferIndex].userData);
				IoctlBuffer[ioctlBufferIndex].state = 0;
				ioctlsRecv++;
				OSWakeupThread(&IoctlBufferThreadQueue);
			}
			else if (ioctlBufferIndex == -1 && ctrlBuffer[nextFrameIndex].callback_sector != 0)
			{
				Console_Write("[DECC] Unknown callback from host. This is impossible.\n");
			}

			for (int i = 0; i < 4; i++)
			{
				if ((ctrlBuffer[lastFrameIndex].status[i] == WPAD_STATUS_DISCONNECTED) != (ctrlBuffer[nextFrameIndex].status[i] == WPAD_STATUS_DISCONNECTED))
				{
					// controller connect or disconnect.
					if (ctrlBuffer[nextFrameIndex].status[i] == WPAD_STATUS_DISCONNECTED)
					{
						autoSamplingBuffer[i] = 0;
						extensionCallback[i] = 0;
						currentFormat[i] = 0;
					}
					if (connectCallback[i] != 0)
					{
						Console_Write("[VISN] Wiimote connect callback. ");
						printInt(i);
						Console_Write(" ");
						printInt(ctrlBuffer[nextFrameIndex].status[i]);
						Console_Write("\n");
						connectCallback[i](i, ctrlBuffer[nextFrameIndex].status[i] == WPAD_STATUS_DISCONNECTED ? WPAD_STATUS_DISCONNECTED : WPAD_STATUS_OK);
					}
				}
				if (ctrlBuffer[lastFrameIndex].extension[i] != ctrlBuffer[nextFrameIndex].extension[i])
				{
					// extension controller changes
					if (extensionCallback[i] != 0)
					{
						if (ctrlBuffer[lastFrameIndex].extension[i] != WPAD_EXTENSION_UNKNOWN)
						{
							extensionCallback[i](i, WPAD_EXTENSION_UNKNOWN);
						}
						if (ctrlBuffer[nextFrameIndex].extension[i] != WPAD_EXTENSION_UNKNOWN)
						{
							Console_Write("[VISN] Wiimote extension callback. ");
							printInt(i);
							Console_Write(" ");
							printInt(ctrlBuffer[nextFrameIndex].extension[i]);
							Console_Write("\n");
							extensionCallback[i](i, ctrlBuffer[nextFrameIndex].extension[i]);
						}
					}
				}
			}
			for (int i = 0; i < 4; i++)
			{
				if (ctrlBuffer[nextFrameIndex].status[i] == 0)
				{
					if (autoSamplingBuffer[i])
					{
						int autoSampleIndex = autoSamplingBufferIndex[i];
						int autoSampleNext = (autoSampleIndex + 1) % autoSamplingBufferCount[i];
						WPADData_t *nextPos = (WPADData_t *)((char *)autoSamplingBuffer[i] + autoSampleNext * WPADDataFormatSize(currentFormat[i]));
						MyWPADRead(i, nextPos);
						autoSamplingBufferIndex[i] = autoSampleNext;
					}
					if (samplingCallback[i] != 0)
					{
						samplingCallback[i](i);
					}
				}
			}

			ctrlBuffer[lastFrameIndex].haveSentOrRecv = false;
			for (int i = 0; i < 4; i++)
			{
				ctrlBuffer[lastFrameIndex].haveInput[i] = false;
			}
			ctrlBuffer[lastFrameIndex].frameNumber += BUFFER_SIZE;
		}
		_CPU_FP_Restore(fp_isr);
		__VIRetraceHandler(isr, context);
	}
	else
	{
		stillFrames++;
		static volatile unsigned short* const _viReg = (volatile unsigned short *)0xCC002000;
		unsigned int intr;
		// WE must clear the interrupt.
		
		intr = _viReg[24];
		if(intr&0x8000) {
			_viReg[24] = intr&~0x8000;
		}

		intr = _viReg[26];
		if(intr&0x8000) {
			_viReg[26] = intr&~0x8000;
		}

		intr = _viReg[28];
		if(intr&0x8000) {
			_viReg[28] = intr&~0x8000;
		}

		intr = _viReg[30];
		if(intr&0x8000) {
			_viReg[30] = intr&~0x8000;
		}
		if (stillFrames > 60)
		{
			Console_Write("[VIRT] Anti Woof! ");
			printInt(frameCount);
			Console_Write("\n");
			stillFrames = 0;
		}
		_CPU_FP_Restore(fp_isr);
	}
}

static int MyWPADSetDataFormat(int wiimote, WPADDataFormat_t format)
{
	currentFormat[wiimote] = format;
	if (wiimote == HOSTS_WIIMOTE_NUMBER)
	{
		Console_Write("[WPAD] Format change for Wiimote 0\n");
		if (host)
		{
			return WPADSetDataFormat(0, format);
		}
		else
		{
			return 0;
		}
	}
	else if (wiimote == CLIENTS_WIIMOTE_NUMBER)
	{
		Console_Write("[WPAD] Format change for Wiimote 1\n");
		if (!host)
		{
			return WPADSetDataFormat(0, format);
		}
		else
		{
			return 0;
		}
	}
	else
	{
		return -1;
	}
}

static WPADDataFormat_t MyWPADGetDataFormat(int wiimote)
{
	return currentFormat[wiimote];
}

static bool dpdEnabled[4];
static WPADControlDpdCallback_t MyWPADControlDpdCallback_callback;

static void MyWPADControlDpdCallback(int wiimote, int status)
{
	if (MyWPADControlDpdCallback_callback != 0)
	{
		MyWPADControlDpdCallback_callback(host ? HOSTS_WIIMOTE_NUMBER : CLIENTS_WIIMOTE_NUMBER, status);
	}
}

static int MyWPADControlDpd(int wiimote, int command, WPADControlDpdCallback_t callback)
{
	if (wiimote == HOSTS_WIIMOTE_NUMBER)
	{
		dpdEnabled[wiimote] = command > 0;
		if (host)
		{
			MyWPADControlDpdCallback_callback = callback;
			return WPADControlDpd(0, command, MyWPADControlDpdCallback);
		}
		else
		{
			if (callback != 0)
			{
				callback(wiimote, 0);
			}
			return 0;
		}
	}
	else if (wiimote == CLIENTS_WIIMOTE_NUMBER)
	{
		dpdEnabled[wiimote] = command > 0;
		if (!host)
		{
			MyWPADControlDpdCallback_callback = callback;
			return WPADControlDpd(0, command, MyWPADControlDpdCallback);
		}
		else
		{
			if (callback != 0)
			{
				callback(wiimote, 0);
			}
			return 0;
		}
	}
	else
	{
		return WPAD_STATUS_DISCONNECTED;
	}
}
static bool MyWPADIsDpdEnabled(int wiimote)
{
	return dpdEnabled[wiimote];
}

#define MAX_THREADS 64
static unsigned short ActualRNG(int id)
{
	static int thread_id[MAX_THREADS];
	static int thread_lastSeenFrame[MAX_THREADS];
	static int thread_counter[MAX_THREADS];

	int isr;
	_CPU_ISR_Disable(isr);
	static int lastIndex = 0;
	int thread_index;

	if (thread_id[lastIndex] == id)
	{
		thread_index = lastIndex;
	}
	else
	{
		for (thread_index = 0; thread_index < MAX_THREADS - 1; thread_index++)
		{
			if (thread_id[thread_index] == id)
			{
				break;
			}
			else if (thread_id[thread_index] == 0)
			{
				thread_id[thread_index] = id;
				break;
			}
		}
		if (thread_index == MAX_THREADS - 1)
		{
			Console_Write("[RNG] That's more threads than I can count!\n");
		}
		lastIndex = thread_index;
	}
	
	int index = frameCount % BUFFER_SIZE;
	if (frameCount - thread_lastSeenFrame[thread_index] > 100)
	{
		thread_counter[thread_index] = ctrlBuffer[index].frameSeed;
		Console_Write("[RNG] Reseeded thread ");
		printInt(thread_index);
		Console_Write(" to value ");
		printInt(thread_counter[thread_index]);
		Console_Write(".\n");
	}
	thread_lastSeenFrame[thread_index] = frameCount;
	thread_counter[thread_index] = thread_counter[thread_index] * 1664525 + 1013904223;

	int val = thread_counter[thread_index];
	
	_CPU_ISR_Restore(isr);

	return (val >> 16) & 0xffff;
}

static int Myrand(void)
{
	int grandparent = (mfsp() == 0 || *(int *)mfsp() == 0 || **(int **)mfsp() == 0) ? 0 : (**(int ***)mfsp())[1];
	int lr = mflr();
	int id = lr + grandparent * 3;
	return ActualRNG(id);
}

static unsigned int MyRNG_Rand(rng_t *rng, unsigned int max)
{
	int grandparent = (mfsp() == 0 || *(int *)mfsp() == 0 || **(int **)mfsp() == 0) ? 0 : (**(int ***)mfsp())[1];
	int lr = mflr();
	int id = lr + grandparent * 3;
	unsigned long long val = ((unsigned long)ActualRNG(id) << 16) | ActualRNG(id);
	*rng = val;
	return (val * max) >> 32;
}
static float MyRNG_RandFloat(rng_t *rng)
{
	int grandparent = (mfsp() == 0 || *(int *)mfsp() == 0 || **(int **)mfsp() == 0) ? 0 : (**(int ***)mfsp())[1];
	int lr = mflr();
	int id = lr + grandparent * 3;
	unsigned long val = ((unsigned long)ActualRNG(id) << 16) | ActualRNG(id);
	*rng = val;
	return (float)val / 4294967296.0f;
}
static float MyRNG_RandFloatZeroed(rng_t *rng)
{
	int grandparent = (**(int ***)mfsp())[1];
	int lr = mflr();
	int id = lr + grandparent * 3;
	unsigned long val = ((unsigned long)ActualRNG(id) << 16) | ActualRNG(id);
	*rng = val;
	return ((float)val / 4294967296.0f) - 0.5f;
}


static unsigned char MySMNDance_GetUnkown_a6c9(void)
{
	int isr;
	_CPU_ISR_Disable(isr);
	int index = frameCount % BUFFER_SIZE;
	unsigned char result = ctrlBuffer[index].game.SMN.dance_a6c9;
	_CPU_ISR_Restore(isr);
	return result;
}
static bool MySMNDance_GetUnkown_a6c9Mask(unsigned char mask)
{
	return MySMNDance_GetUnkown_a6c9() == mask;
}
static unsigned char MySMNDance_GetMask(void)
{
	int isr;
	_CPU_ISR_Disable(isr);
	int index = frameCount % BUFFER_SIZE;
	unsigned char result = ctrlBuffer[index].game.SMN.dance_a6ca;
	_CPU_ISR_Restore(isr);
	return result;
}
static bool MySMNDance_Enabled(unsigned char mask)
{
	return (MySMNDance_GetMask() & mask) != 0;
}
static unsigned char MySMNDance_GetUnkown_a6cb(void)
{
	int isr;
	_CPU_ISR_Disable(isr);
	int index = frameCount % BUFFER_SIZE;
	unsigned char result = ctrlBuffer[index].game.SMN.dance_a6cb;
	_CPU_ISR_Restore(isr);
	return result;
}

#ifndef NDEBUG
static void Debug_CheckFrameAdvanceHandler(bool skip)
{
	Debug_CheckVariable++;
}
#endif
