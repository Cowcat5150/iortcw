/*
 * Copyright (C) 2005 Mark Olsen
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <exec/exec.h>
#include <exec/system.h>
#include <intuition/intuition.h>
#include <dos/dosextens.h>

#include <proto/exec.h>
#include <proto/intuition.h>

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../renderer/tr_public.h"
#include "../client/client.h"

#include "morphos_in.h"
#include "morphos_glimp.h"

#define MAXIMSGS 32

#ifdef DEDICATED
#define BINNAME "MorphOSQ3Ded"
#else
#define BINNAME "MorphOSQuake3"
#endif

const char verstring[] = "$VER: "BINNAME" 1.0 (30.8.2005) By Mark Olsen, based on Quake 3 1.32b by Id Software";

int __stack = 1024*1024;

static int consoleinput;
static int consoleoutput;
static int consoleoutputinteractive;

static BPTR olddir;

int use_altivec;
struct Library *SocketBase;

/* For NET_Sleep */
int stdin_active = 0;


void Sys_Quit()
{
	//CL_Shutdown();
	//IN_Shutdown(); // now in glimp - Cowcat

	if (SocketBase)
		CloseLibrary(SocketBase);

	if (consoleinput && consoleoutputinteractive)
		SetMode(Input(), 0);

	if (olddir)
	{
		UnLock(CurrentDir(olddir));
	}

	exit(0);
}

static void ErrorMessage(char *string)
{
	char msg[4096];

	snprintf(msg, sizeof(msg), "%s", string);

	if (consoleoutput)
		fprintf(stderr, "%s", msg);
	else
	{
		struct EasyStruct es;

		if (msg[strlen(msg)-1] == '\n')
			msg[strlen(msg)-1] = 0;

		es.es_StructSize = sizeof(es);
		es.es_Flags = 0;
		es.es_Title = "Quake III: Arena";
		es.es_TextFormat = msg;
		es.es_GadgetFormat = "Quit";

		EasyRequest(0, &es, 0, 0);
	}
}

void Sys_Error(const char *fmt, ...)
{
	va_list va;
	char msg[4096];

	strcpy(msg, "Sys_Error: ");

	va_start(va, fmt);
	vsnprintf(msg+strlen(msg), sizeof(msg)-strlen(msg), fmt, va);
	va_end(va);

	ErrorMessage(msg);

	Sys_Quit();
}

void Sys_Print(const char *msg)
{
	if (consoleoutput)
		fputs(msg, stdout);
}

qboolean Sys_LowPhysicalMemory()
{
	/* This is only used for touching memory, there's really no need for that... */

	return qtrue;
}

void Sys_BeginProfiling()
{
}

void Sys_InitStreamThread()
{
}

void Sys_ShutdownStreamThread()
{
}

void Sys_BeginStreamedFile(fileHandle_t f, int readAhead)
{
}

void Sys_EndStreamedFile(fileHandle_t f)
{
}

int Sys_StreamedRead(void *buffer, int size, int count, fileHandle_t f)
{
	return FS_Read( buffer, size * count, f );
}

void Sys_StreamSeek(fileHandle_t f, int offset, int origin)
{
	FS_Seek(f, offset, origin);
}

char *Sys_GetClipboardData()
{
	return 0;
}

int putenv __P((const char *name))
{
	return 1;
}


#if 0

sysEvent_t Sys_GetEvent()
{
	sysEvent_t ev;
	static char inbuf[256];
	static int inbufsize;

	msg_t netmsg;
	byte netpacket[MAX_MSGLEN];
	netadr_t adr;

	memset(&ev, 0, sizeof(ev));
	ev.evTime = Sys_Milliseconds();

	if (consoleinput && consoleoutputinteractive)
	{
		while(WaitForChar(Input(), 0) && Read(Input(), inbuf+inbufsize, 10) == 1)
		{
			if (inbuf[inbufsize] == 3)
			{
				Signal(FindTask(0), SIGBREAKF_CTRL_C);
				continue;
			}

			if (inbuf[inbufsize] == 8 && inbufsize != 0)
			{
				FPutC(Output(), 8);
				FPutC(Output(), ' ');
				FPutC(Output(), 8);
				Flush(Output());
				inbufsize--;
				continue;
			}

			if (inbuf[inbufsize] == '\r')
				inbuf[inbufsize] = '\n';

			else if (!isprint(inbuf[inbufsize]))
				continue;

			FPutC(Output(), inbuf[inbufsize]);
			Flush(Output());
			inbufsize++;

			if (inbuf[inbufsize-1] == '\n' || inbufsize == sizeof(inbuf)-11)
			{
				inbuf[inbufsize] = 0;
				ev.evType = SE_CONSOLE;
				ev.evPtr = Z_Malloc(inbufsize+1);
				ev.evPtrLength = inbufsize+1;

				strcpy(ev.evPtr, inbuf);

				inbufsize = 0;

				return ev;
			}
		}
	}

	else if (consoleinput)
	{
		while(WaitForChar(Input(), 0))
		{
			if (FGets(Input(), inbuf, sizeof(inbuf)))
			{
				ev.evType = SE_CONSOLE;
				ev.evPtr = Z_Malloc(strlen(inbuf)+1);
				ev.evPtrLength = strlen(inbuf)+1;

				strcpy(ev.evPtr, inbuf);

				return ev;
			}
		}
	}

	MSG_Init(&netmsg, netpacket, sizeof(netpacket));

	if (Sys_GetPacket(&adr, &netmsg))
	{
		netadr_t *buf;
		int len;

		len = sizeof(netadr_t) + netmsg.cursize;
		buf = Z_Malloc( len );
		*buf = adr;
		memcpy( buf+1, netmsg.data, netmsg.cursize );
		ev.evType = SE_PACKET;
		ev.evPtr = buf;
		ev.evPtrLength = len;

		return ev;
	}

#ifndef DEDICATED
	IN_GetEvent(&ev);
#endif

	return ev;
}

#else


#endif

qboolean Sys_CheckCD()
{
	return qtrue;
}

#if 0
char *Sys_GetDLLName( const char *name )
{
	return va("%s.sp." ARCH_STRING DLL_EXT, name);
}
#endif

#if 1
//void *Sys_LoadDll(const char *name, char *fqpath, int (**entryPoint)(int, ...), int (*systemcalls)(int, ...))
//void *Sys_LoadDll( const char *name, char *fqpath, intptr_t(**entryPoint)(int, int, int, int ), intptr_t(*systemcalls)(intptr_t, ...) ) // Cowcat
//void *Sys_LoadDll( const char *name, char *fqpath, intptr_t (QDECL **entryPoint)(intptr_t, ...), intptr_t (QDECL *systemcalls)(intptr_t, ...) )
void * QDECL Sys_LoadDll( const char *name, intptr_t(**entryPoint)(intptr_t, ...), intptr_t(*systemcalls)(intptr_t, ...) )
{
	return 0;
}
#endif

void Sys_UnloadDll(void *dllHandle)
{
}

char *Sys_GetDLLName( const char *name )
{
	return va("%s.sp." ARCH_STRING DLL_EXT, name);
}

static char *ProcessorName()
{
	ULONG CPUType = MACHINE_PPC;

	NewGetSystemAttrs(&CPUType,sizeof(CPUType),SYSTEMINFOTYPE_MACHINE,TAG_DONE);

	if (CPUType == MACHINE_PPC)
	{
		ULONG CPUVersion, CPURevision;

		if (NewGetSystemAttrs(&CPUVersion,sizeof(CPUVersion),SYSTEMINFOTYPE_PPC_CPUVERSION,TAG_DONE)
		 && NewGetSystemAttrs(&CPURevision,sizeof(CPURevision),SYSTEMINFOTYPE_PPC_CPUREVISION,TAG_DONE))
		{
			switch (CPUVersion)
			{
				case 0x0001:
					return "601";
				case 0x0003: 
					return "603";
				case 0x0004:
					return "604";
				case 0x0006:
					return "603E";
				case 0x0007:
					return "603R/603EV";
				case 0x0008:
					if ((CPURevision & 0xf000) == 0x2000)
					{
						if (CPURevision >= 0x2214)
							return "750CXE (G3)";
						else
							return "750CX (G3)";
					}
					else
						return "740/750 (G3)";
				case 0x0009:
					return "604E";
				case 0x000A:
					return "604EV";
				case 0x000C:
					if (CPURevision & 0x1000)
						return "7410 (G4)";
					else
						return "7400 (G4)";
				case 0x0039:
					return "970 (G5)";
				case 0x003C:
					return "970FX (G5)";
				case 0x8000:
					if (CPURevision > 0x0200)
						return "7451 (G4)";
					else
						return "7441/7450 (G4)";
				case 0x8001:
					return "7445/7455 (G4)";
				case 0x8002:
					return "7447/7457 (G4)";
				case 0x8003:
					return "7447A (G4)";
				case 0x8004:
					return "7448 (G4)";
				case 0x800C:
					return "7410 (G4)";
				default:
					return "Unknown";
			}
		}
	}

	return "Unknown";
}

/*
=================
Sys_In_Restart_f

Restart the input subsystem
=================
*/
void Sys_In_Restart_f( void ) 
{
  	IN_Restart();
}


void Sys_Init()
{
	Cvar_Set("arch", "morphos ppc");

	Cvar_Set("sys_cpustring", ProcessorName());

	Cmd_AddCommand ("in_restart", Sys_In_Restart_f);

	//IN_Init(); // now in glimp - Cowcat
}

int main(int argc, char **argv)
{
	struct Resident *morphos;
	int r1, r2;

	int i, len;

	char *cmdline;

	BPTR l;

	extern struct WBStartup *_WBenchMsg;

	BPTR lock;

	if (!_WBenchMsg)
	{
		consoleoutput = 1;

		if (IsInteractive(Output()))
			consoleoutputinteractive = 1;
	}

	l = Lock("PROGDIR:", ACCESS_READ);

	if (l)
		olddir = CurrentDir(l);

	#if 0 // Cowcat
	for(i=0;pakfiles[i] != 0;i++)
	{
		lock = Lock(pakfiles[i], ACCESS_READ);

		if (lock)
			UnLock(lock);
		else
			break;
	}

	if (pakfiles[i] != 0)
	{
		ErrorMessage("Incomplete installation. Please run the installer.\n");
		return 0;
	}
	#endif

	morphos = FindResident("MorphOS");

	r1 = NewGetSystemAttrs(&r2, sizeof(r2), SYSTEMINFOTYPE_PPC_ALTIVEC);

	if (r1 == sizeof(r2) && r2 != 0 && morphos && (morphos->rt_Flags&RTF_EXTENDED) && (morphos->rt_Version > 1 || (morphos->rt_Version == 1 && morphos->rt_Revision >= 5)))
		use_altivec = 1;

	signal(SIGINT, SIG_IGN);

	SocketBase = OpenLibrary("bsdsocket.library", 0);

#ifdef DEDICATED
	if (SocketBase == 0)
	{
		printf("Unable to open bsdsocket.library\n");
		return 0;
	}
#endif

	//Sys_SetDefaultCDPath(argv[0]);

	len = 1;

	for(i=1;i<argc;i++)
		len+= strlen(argv[i])+1;

	cmdline = malloc(len);

	if (cmdline == 0)
		Sys_Quit();

	cmdline[0] = 0;

	for(i=1;i<argc;i++)
	{
		strcat(cmdline, argv[i]);

		if (i != argc-1)
			strcat(cmdline, " ");
	}

	Com_Init(cmdline);

	if (SocketBase)
		NET_Init();

	if (!_WBenchMsg && com_dedicated && com_dedicated->value && IsInteractive(Input()))
	{
		consoleinput = 1;

		if (consoleoutputinteractive)
			SetMode(Input(), 1);
	}

	while((SetSignal(0, 0)&SIGBREAKF_CTRL_C) == 0)
	{
		Com_Frame();

#ifndef DEDICATED
		GLimp_Frame();
#endif
	}

	Sys_Quit();

	return 0;
}

cpuFeatures_t Sys_GetProcessorFeatures( void )
{
	return 0; //TODO: should return altivec if available
}

char *Sys_ConsoleInput(void) // Cowcat
{
	return NULL;
}

qboolean Sys_RandomBytes( byte *string, int len ) // Cowcat
{
	return qfalse;
}