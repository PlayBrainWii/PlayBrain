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

#include "main.h"

#ifdef _WIN32
#undef _WIN32
#endif

#include <fat.h>
#include <malloc.h>
#include <ogc/consol.h>
#include <ogc/lwp.h>
#include <ogc/system.h>
#include <ogc/video.h>
#include <sdcard/wiisd_io.h>
#include <stdio.h>
#include <stdlib.h>
#include <wiiuse/wpad.h>

#include "apploader/apploader.h"
#include "library/dolphin_os.h"
#include "library/event.h"
#include "modules/module.h"
#include "network.h"
#include "search/search.h"
#include "settings/settings.h"
#include "threads.h"

event_t main_event_fat_loaded;

static void Main_PrintSize(size_t size);
static void ConnectHOST(void);
static void ConnectGUEST(void);

int host_ip = 0xc0a80121;
int port = 10000;

int main(void) {
    int ret;
    void *frame_buffer = NULL;
    GXRModeObj *rmode = NULL;   
	
    /* The game's boot loader is statically loaded at 0x81200000, so we'd better
     * not start mallocing there! */
    SYS_SetArena1Hi((void *)0x81200000);
	/* initialise Wii Remotes?! */
	WPAD_Init();
	settings_init();
	
    /* initialise all subsystems */
    if (!Event_Init(&main_event_fat_loaded))
        goto exit_error;
    if (!Apploader_Init())
        goto exit_error;
    if (!Module_Init())
        goto exit_error;
    if (!Search_Init())
        goto exit_error;
    
    /* main thread is UI, so set thread prior to UI */
    LWP_SetThreadPriority(LWP_GetSelf(), THREAD_PRIO_UI);

    /* configure the video */
    VIDEO_Init();
    
    rmode = VIDEO_GetPreferredMode(NULL);
    
    frame_buffer = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    if (!frame_buffer)
        goto exit_error;
    console_init(
        frame_buffer, 20, 20, rmode->fbWidth, rmode->xfbHeight,
        rmode->fbWidth * VI_DISPLAY_PIX_SZ);

    /* spawn lots of worker threads to do stuff */
    if (!Apploader_RunBackground())
        goto exit_error;
    if (!Module_RunBackground())
        goto exit_error;
    if (!Search_RunBackground())
        goto exit_error;
        
    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(frame_buffer);
    VIDEO_SetBlack(false);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE)
        VIDEO_WaitVSync();

    /* display the welcome message */
    printf("\x1b[2;0H");
    printf("PlayBrain by JBMagination and others");
	printf("Wii NetPlay Test Edition by MrBean35000vr and Chadderz\n");
    printf("Based on BrainSlug Wii v%x.%02x.%04x"
#ifndef NDEBUG
        " DEBUG build"
#endif
#ifndef NRELEASE
        " RELEASE build"
#endif
        "\n",
        BSLUG_VERSION_MAJOR(BSLUG_LOADER_VERSION),
        BSLUG_VERSION_MINOR(BSLUG_LOADER_VERSION),
        BSLUG_VERSION_REVISION(BSLUG_LOADER_VERSION));
    printf(" by Chadderz\n\n");

	printf("This software will attempt to patch the inserted disc for NetPlay.\n"
		"This is an extremely early build! It probably doesn't work with many games!\n"
		"It is currently designed primarily for New Super Mario Bros. Wii, and works\nreasonably well.\n"
		"It would be best to be in verbal contact with the other players to work out if\na desync has occurred.\n"
		"Save files are NOT synced, so make sure everyone has the same one!\n"
		"All settings, including host IP and port, should be set manually in the\n" APP_PATH "/config.ini file.\n"
		"Currently, only two players are supported.\n\n");

   
    if (!__io_wiisd.startup() || !__io_wiisd.isInserted()) {
        printf("Please insert an SD card.\n\n");
        do {
            __io_wiisd.shutdown();
        } while (!__io_wiisd.startup() || !__io_wiisd.isInserted());
    }
    __io_wiisd.shutdown();
    
    if (!fatMountSimple("sd", &__io_wiisd)) {
        fprintf(stderr, "Could not mount SD card.\n");
        goto exit_error;
    }
    
    Event_Trigger(&main_event_fat_loaded);
	
	settings_load();
    
    printf("Waiting for game disk...\n");
    Event_Wait(&apploader_event_disk_id);
	printf("Game ID: %.4s Version %d\nMake sure all players use the same version!\n", os0->disc.gamename, (int)os0->disc.gamever + 1);
        
    printf("Loading modules...\n");
    Event_Wait(&module_event_list_loaded);
    if (module_list_count == 0) {
        printf("No valid modules found!\nCheck the files are in the right place!\n");
		printf("\nPress RESET to exit.\n");
        goto exit_error;
    } else {
        size_t module;
        
        printf(
            "%u module%s found.\n",
            module_list_count, module_list_count > 1 ? "s" : "");
        
        for (module = 0; module < module_list_count; module++) {
            printf(
                "\t%s %s by %s (", module_list[module]->name,
                module_list[module]->version, module_list[module]->author);
            Main_PrintSize(module_list[module]->size);
            puts(").");
        }
        
        Main_PrintSize(module_list_size);
        puts(" total.");
    }
    
	printf("\nPlease wait while the game is patched.\nIf nothing happens after about 2 minutes, reset the machine!\n");

    Event_Wait(&apploader_event_complete);
    Event_Wait(&module_event_complete);
    fatUnmount("sd");
    __io_wiisd.shutdown();
    
    if (module_has_error) {
        printf("\nPress RESET to exit.\n");
        goto exit_error;
    }
    
    if (apploader_game_entry_fn == NULL) {
        fprintf(stderr, "Error... entry point is NULL.\n");
    } else {
        if (module_has_info || search_has_info) {
            printf("\nPress RESET to launch game.\n");
            
            while (!SYS_ResetButtonDown())
                VIDEO_WaitVSync();
            while (SYS_ResetButtonDown())
                VIDEO_WaitVSync();
        }
		
		printf("\nReady! Please select on your Wii Remote:\nA button: Host, B button: Guest\n(Make sure the host goes first).\n");
		while(1)
		{
			WPAD_ScanPads();
			if(WPAD_ButtonsDown(0) & WPAD_BUTTON_A)
			{
				printf("HOST. Please wait, initialising network...\n");
				ConnectHOST();
				break;
			}
			else if(WPAD_ButtonsDown(0) & WPAD_BUTTON_B)
			{
				printf("GUEST. Please wait, initialising network...\n");
				ConnectGUEST();
				break;
			}
		}
        
        SYS_ResetSystem(SYS_SHUTDOWN, 0, 0);
        apploader_game_entry_fn();
    }

    ret = 0;
    goto exit;
exit_error:
    ret = -1;
exit:
    while (!SYS_ResetButtonDown())
        VIDEO_WaitVSync();
    while (SYS_ResetButtonDown())
        VIDEO_WaitVSync();
    
    VIDEO_SetBlack(true);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    
    free(frame_buffer);
        
    exit(ret);
        
    return ret;
}

static void Main_PrintSize(size_t size) {
    static const char *suffix[] = { "bytes", "KiB", "MiB", "GiB" };
    unsigned int magnitude, precision;
    float sizef;

    sizef = size;
    magnitude = 0;
    while (sizef > 512) {
        sizef /= 1024.0f;
        magnitude++;
    }
    
    assert(magnitude < 4);
    
    if (magnitude == 0)
        precision = 0;
    else if (sizef >= 100)
        precision = 0;
    else if (sizef >= 10)
        precision = 1;
    else
        precision = 2;
        
    printf("%.*f %s", precision, sizef, suffix[magnitude]);
}

static void ConnectHOST(void)
{
	if(Mynet_init())
	{
		goto error;
	}
	int sock = Mynet_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);

	if (sock == -1)
	{
		goto error;
	}

	struct sockaddr_in myAddress = {};
	myAddress.sin_family = AF_INET;
	myAddress.sin_len = 8;
	myAddress.sin_port = port;
	myAddress.sin_addr.s_addr = Mynet_gethostip();

	if(Mynet_bind(sock, (struct sockaddr*)&myAddress, myAddress.sin_len))
	{
		goto error;
	}

	if(Mynet_listen(sock, 1))
	{
		goto error;
	}

	printf("Listening on %d.%d.%d.%d:%d...\n", (int)((myAddress.sin_addr.s_addr >> 24) & 0xff), (int)((myAddress.sin_addr.s_addr >> 16) & 0xff), (int)((myAddress.sin_addr.s_addr >> 8) & 0xff), (int)(myAddress.sin_addr.s_addr & 0xff), port);

	struct sockaddr_in theirAddress = {};
	int theirAddressLength = 8;

	int communicationSock = Mynet_accept(sock, (struct sockaddr*)&theirAddress, (u32*)&theirAddressLength);

	if (communicationSock == -1)
	{
		goto error;
	}
	
	int on = 0;
	Mynet_setsockopt(communicationSock, 0, TCP_NODELAY, (char *) &on, sizeof(on));

	printf("Connected! Sending start request!");

	int letsgo = 1;

	if(Mynet_send(communicationSock, &letsgo, sizeof(letsgo), 0) != sizeof(letsgo))
	{
		goto error;
	}

	printf(" OK!\nWaiting for response...");

	int whatigot = 0;

	if(Mynet_recv(communicationSock, &whatigot, sizeof(whatigot), 0) != sizeof(whatigot))
	{
		goto error;
	}

	printf(" OK!\n");

	if (whatigot == 1)
	{
		printf("That was good.\n");
		int* sockPointer = (int*)Search_SymbolLookup("communicationSock");
		*sockPointer = communicationSock;
		int* net_ip_top_fd_pointer = (int*)Search_SymbolLookup("net_ip_top_fd");
		*net_ip_top_fd_pointer = net_ip_top_fd;
		int* host_pointer = (int*)Search_SymbolLookup("host");
		*host_pointer = 1;
		return;
	}
	else
	{
		goto error;
	}

error:
	printf("\nWell, that didn't work! Press RESET to get us out of here and try again.\nIf you don't have a RESET button, I feel sorry for you.\n");
	while (!SYS_ResetButtonDown())
        VIDEO_WaitVSync();
    while (SYS_ResetButtonDown())
        VIDEO_WaitVSync();
	exit(0);
	
	
}

static void ConnectGUEST(void)
{
if(Mynet_init())
	{
		goto error;
	}
	int sock = Mynet_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);

	if (sock == -1)
	{
		goto error;
	}

	struct sockaddr_in hostAddress = {};
	hostAddress.sin_family = AF_INET;
	hostAddress.sin_len = 8;
	hostAddress.sin_port = port;
	hostAddress.sin_addr.s_addr = host_ip;

	printf("Connecting to %d.%d.%d.%d:%d...\n", (int)((host_ip >> 24) & 0xff), (int)((host_ip >> 16) & 0xff), (int)((host_ip >> 8) & 0xff), (int)(host_ip & 0xff), port);

	if(Mynet_connect(sock, (struct sockaddr*)&hostAddress, hostAddress.sin_len))
	{
		goto error;
	}
	
	int on = 0;
	Mynet_setsockopt(sock, 0, TCP_NODELAY, (char *) &on, sizeof(on));



	printf("Connected! Waiting for start request!");

	int whatigot = 0;

	if(Mynet_recv(sock, &whatigot, sizeof(whatigot), 0) != sizeof(whatigot))
	{
		goto error;
	}



	printf(" OK!\nSending a response...");

	int letsgo = 1;

	if(Mynet_send(sock, &letsgo, sizeof(letsgo), 0) != sizeof(letsgo))
	{
		goto error;
	}

	printf(" OK!\n");

	if (whatigot == 1)
	{
		printf("That was good.\n");
		int* sockPointer = (int*)Search_SymbolLookup("communicationSock");
		*sockPointer = sock;
		int* net_ip_top_fd_pointer = (int*)Search_SymbolLookup("net_ip_top_fd");
		*net_ip_top_fd_pointer = net_ip_top_fd;
		int* host_pointer = (int*)Search_SymbolLookup("host");
		*host_pointer = 0;
		return;
	}
	else
	{
		goto error;
	}

error:
	printf("\nWell, that didn't work! Press RESET to get us out of here and try again.\nIf you don't have a RESET button, I feel sorry for you.\n");
	while (!SYS_ResetButtonDown())
        VIDEO_WaitVSync();
    while (SYS_ResetButtonDown())
        VIDEO_WaitVSync();
	exit(0);
}
