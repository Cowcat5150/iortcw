#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../renderer/tr_public.h"
#include "../client/client.h"

#include "amiga_local.h"

#ifdef __VBCC__
#pragma amiga-align
#elif defined(WARPUP)
#pragma pack(2)
#endif

#include <exec/exec.h>
#include <exec/memory.h> // new
#include <exec/ports.h>
#include <intuition/intuition.h>
#include <dos/dos.h>
#include <devices/timer.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/timer.h>
#include <proto/intuition.h>

#ifdef __PPC__
#if defined(__GNUC__)
#include <powerpc/powerpc_protos.h>
#else
#include <powerpc/powerpc.h>
#include <proto/powerpc.h>
#endif
#endif

#ifdef __VBCC__
#pragma default align
#elif defined (WARPUP)
#pragma pack()
#endif

struct Library *SocketBase;

unsigned long __stack = 0x2000000; // auto stack Cowcat

int main(int argc, char **argv)
{
	char 	*cmdline;
	int 	i, len;
	//int	startTime, endTime;

	if(SocketBase == NULL)
		SocketBase = OpenLibrary("bsdsocket.library",0L);

	//Sys_CreateConsole();

	Sys_Milliseconds();
	
	//Sys_SetBinaryPath( Sys_Dirname( argv[ 0 ] ) );
	//Sys_SetDefaultInstallPath( DEFAULT_BASEDIR );

	// merge the command line, this is kinda silly
	for (len = 1, i = 1; i < argc; i++)
		len += strlen(argv[i]) + 1;

	cmdline = (char *)malloc(len);
	*cmdline = 0;

	for (i = 1; i < argc; i++)
	{
		if (i > 1)
			strcat(cmdline, " ");

		strcat(cmdline, argv[i]);
	}

	//memset( &eventQue[0], 0, MAX_QUED_EVENTS*sizeof(sysEvent_t) ); 
	//memset( &sys_packetReceived[0], 0, MAX_MSGLEN*sizeof(byte) );

	Com_Init(cmdline);

	free(cmdline);
	cmdline = NULL;

	NET_Init();

	while( 1 ) 
	{
		//startTime = Sys_Milliseconds();

		// make sure mouse and joystick are only called once a frame
		//IN_Frame(); // now called in common.c - Com_Frame

		// run the game
		Com_Frame();

		//endTime = Sys_Milliseconds();
		//totalMsec += endTime - startTime;
		//countMsec++;
	}

	return 0; 
}
