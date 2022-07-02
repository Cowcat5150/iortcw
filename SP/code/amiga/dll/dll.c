/*
** This file contains the runtime usable DLL functions, like LoadLibrary, GetProcAddress etc.
*/

#define __DLL_LIB_BUILD

//#include "dll.h"

#pragma pack(push,2)

#include <dos/dos.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>

#if defined(__PPC__)

#include <exec/exec.h>
#include <exec/memory.h>

#if defined(__GNUC__)
#include <powerpc/powerpc.h>
#include <powerpc/powerpc_protos.h>
#else
#include <powerpc/powerpc.h>
#include <proto/powerpc.h>
#endif

#endif

#pragma pack(pop)

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "dll.h"

#define bzero(p, l) memset(p, 0, l)

#if defined(__PPC__)

#undef CreateMsgPort
#undef DeleteMsgPort
#undef GetMsg
#undef FindPort
#undef WaitPort
#undef PutMsg

#define CreateMsgPort CreateMsgPortPPC
#define DeleteMsgPort DeleteMsgPortPPC
#define GetMsg GetMsgPPC
#define FindPort FindPortPPC
#define WaitPort WaitPortPPC
#define PutMsg PutMsgPPC

#endif

void dllInternalFreeLibrary(int);

#define DLLOPENDLLS_MAX 20

struct dllOpenedDLL
{
	struct dll_sInstance	*inst;
	int			usecount;
	char			name[100];
};

struct dllOpenedDLL dllOpenedDLLs[DLLOPENDLLS_MAX];	//Maybe better use a linked list, but should work for now

void dllCleanup()
{
	int i;

	for(i=0;i<DLLOPENDLLS_MAX;i++)
	{
		if(dllOpenedDLLs[i].inst)
			dllInternalFreeLibrary(i);
	}
}

void *dllLoadLibrary( char *filename, char *portname )
{
	int (*Entry)(void *, long, void *);
	void *hinst = dllInternalLoadLibrary(filename, portname, 1L);

	if (!hinst)
	{
		return NULL;
	}

	// Check for an entry point
	Entry = dllGetProcAddress(hinst, "DllEntryPoint");

	if (Entry)
	{
		int ret = Entry(hinst, 0, NULL);

		if (ret)
		{
			// if we get non-null here, assume the initialisation worked
			return hinst;
		}

		else
		{
			// the entry point reported an error
			dllFreeLibrary(hinst);
			return NULL;
		}
	}

	return hinst;
}


void *dllInternalLoadLibrary( char *filename, char *portname, int raiseusecount )
{
	struct dll_sInstance	*inst;

	#if defined(__PPC__)
	struct MsgPortPPC	*dllport;
	struct MsgPortPPC	*myport;
	#else
	struct MsgPort		*dllport;
	struct MsgPort		*myport;
	#endif

	dll_tMessage		msg, *reply;
	static int		cleanupflag = 0;
	BPTR			handle;
	int			i;

	if( !cleanupflag )
	{
		bzero(&dllOpenedDLLs,sizeof(dllOpenedDLLs));
			
		if( atexit((void *)dllCleanup) )
			return 0L;

		else
			cleanupflag = 1L;
	}

	if( !filename )
	{
		return 0L;  //Paranoia
	}

	if( !(handle = Open(filename, MODE_OLDFILE)) )
		return 0L;

	Close(handle);

	if( !portname )
		portname = filename;

	// Search for already opened DLLs
	for(i=0; i<DLLOPENDLLS_MAX; i++)
	{
		if( dllOpenedDLLs[i].inst )
		{
			if( strcmp( dllOpenedDLLs[i].name,portname) == 0 )
			{
				if(raiseusecount)
					dllOpenedDLLs[i].usecount++;

				return dllOpenedDLLs[i].inst;
			}
		}
	}

	// Not opened yet search a free slot
	for(i=0; i<DLLOPENDLLS_MAX; i++)
	{
		if( !dllOpenedDLLs[i].inst )
			break;
	}

	if( i == DLLOPENDLLS_MAX )
		return 0L;  // No free slot available

	if( !(inst = malloc(sizeof(struct dll_sInstance))) )
		return 0L;

	if( !(myport = CreateMsgPort()) )
	{
		free(inst);
		return 0L;
	}

	if( !(dllport = FindPort( (unsigned char *)portname)) )
	{
		BPTR output = Open("CON:0/0/800/600/DLL_OUTPUT/AUTO/CLOSE/WAIT", MODE_NEWFILE);
		char commandline[256];
		int i;

		strcpy(commandline, filename);
		strcat(commandline, " ");
		strcat(commandline, portname);

		SystemTags(commandline,
			SYS_Asynch, TRUE,
			SYS_Output, output,
			SYS_Input,  0,		//FIXME: some dll's might need stdin // was NULL - Cowcat
			NP_StackSize, 10000,	//Messagehandler doesn't need a big stack (FIXME: but DLL_(De)Init might)
			TAG_DONE);

		for (i=0; i<20; i++)
		{
			dllport = FindPort( (unsigned char *)portname );

			if ( dllport )
				break;

			//printf("Delaying...\n");
			Delay(25L);
		}
	}

	if( !dllport )
	{
		DeleteMsgPort(myport);

		free(inst);
		return 0L;
	}

	inst->dllPort = dllport;
	inst->StackType = DLLSTACK_DEFAULT;

	bzero(&msg, sizeof(msg));

	msg.dllMessageType = DLLMTYPE_Open;
	msg.dllMessageData.dllOpen.StackType = inst->StackType;

	#if defined(__PPCs__)
	msg.Message.mn_ReplyPort = (struct MsgPortPPC *)myport->mp;
	#else
	msg.Message.mn_ReplyPort = (struct MsgPort *)myport;
	#endif

	PutMsg(dllport, (struct Message *)&msg);
	WaitPort(myport);

	reply = (dll_tMessage *)GetMsg(myport);

	if (reply)
	{
		if( reply->dllMessageData.dllOpen.ErrorCode != DLLERR_NoError )
		{
			DeleteMsgPort(myport);

			free(inst);
			return 0L;
		}
		
		#if 0 // Cowcat
		//Obligatory symbol exports
		inst->FindResource = dllGetProcAddress(inst,"dllFindResource");
		inst->LoadResource = dllGetProcAddress(inst,"dllLoadResource");
		inst->FreeResource = dllGetProcAddress(inst,"dllFreeResource");

		if((inst->FindResource == 0L)|| (inst->LoadResource == 0L)|| (inst->FreeResource == 0L))
		{
			DeleteMsgPort(myport);
			dllOpenedDLLs[i].inst = inst;
			dllInternalFreeLibrary(i);
			return 0L;
		}
		#endif
	}

	else
	{
		//FIXME: Must/Can I send a Close message here ??
		DeleteMsgPort(myport);

		free(inst);
		return 0L;
	}

	DeleteMsgPort(myport);

	dllOpenedDLLs[i].inst = inst;
	dllOpenedDLLs[i].usecount = 1;
	strcpy(dllOpenedDLLs[i].name, portname);

	return inst;
}

void dllFreeLibrary(void *hinst)
{
	int i;

	for(i=0; i<DLLOPENDLLS_MAX; i++)
	{
		if( dllOpenedDLLs[i].inst == hinst )
			break;
	}

	if( i == DLLOPENDLLS_MAX )
		return;		// ?????

	dllOpenedDLLs[i].usecount--;

	if( dllOpenedDLLs[i].usecount <= 0 )
		dllInternalFreeLibrary(i);
}

void dllInternalFreeLibrary(int i)
{
	dll_tMessage		msg, *reply;

	#if defined(__PPC__)
	struct MsgPortPPC	*myport;
	#else
	struct MsgPort		*myport;
	#endif

	struct dll_sInstance	*inst = (struct dll_sInstance *)dllOpenedDLLs[i].inst;
	
	if(!inst)
		return;

	if( !(myport = CreateMsgPort()) )
	{
		exit(0L);	//Arghh
	}

	bzero(&msg, sizeof(msg));

	msg.dllMessageType = DLLMTYPE_Close;

	#if defined (__PPCs__)
	msg.Message.mn_ReplyPort = (struct MsgPortPPC *)myport;
	#else
	msg.Message.mn_ReplyPort = (struct MsgPort *)myport;
	#endif
	
	if( FindPort( (unsigned char *)dllOpenedDLLs[i].name) == inst->dllPort )
	{
		PutMsg( inst->dllPort, (struct Message *)&msg );

		/*WaitPort(myport);*/

		while( !(reply = (dll_tMessage *)GetMsg(myport)) )
		{
			Delay(2);

			if( FindPort( (unsigned char *)dllOpenedDLLs[i].name) != inst->dllPort )
				break;
		}
	}

	DeleteMsgPort(myport);

	free(inst);

	bzero(&dllOpenedDLLs[i],sizeof(dllOpenedDLLs[i]));

	return;
}

void *dllGetProcAddress(void *hinst, char *name)
{
	dll_tMessage		msg, *reply;

	#if defined(__PPC__)
	struct MsgPortPPC	*myport;
	#else
	struct MsgPort		*myport;
	#endif

	struct dll_sInstance	*inst = (struct dll_sInstance *)hinst;
	void			*sym;

	if( !hinst )
	{
		return NULL;
	}

	if( !(myport = CreateMsgPort()) )
	{
		return NULL;
	}

	bzero(&msg, sizeof(msg));
	
	msg.dllMessageType = DLLMTYPE_SymbolQuery;
	msg.dllMessageData.dllSymbolQuery.StackType = inst->StackType;
	msg.dllMessageData.dllSymbolQuery.SymbolName = name;
	msg.dllMessageData.dllSymbolQuery.SymbolPointer = &sym;

	#if defined (__PPCs__)
	msg.Message.mn_ReplyPort = (struct MsgPortPPC *)myport;
	#else
	msg.Message.mn_ReplyPort = (struct MsgPort *)myport;
	#endif

	PutMsg(inst->dllPort, (struct Message *)&msg);
	WaitPort(myport);

	reply = (dll_tMessage *)GetMsg(myport);

	DeleteMsgPort(myport);

	if(reply)
	{
		return(sym);
	}

	return NULL;
}

#if 0
int dllKillLibrary(char *portname)
{
	dll_tMessage	msg,*reply;
	struct MsgPort	*myport;
	struct MsgPort	*dllport;

	if(!(myport = CreateMsgPort()))
		//exit(0L);	//Arghh
		return 0;
	
	bzero(&msg, sizeof(msg));

	msg.dllMessageType = DLLMTYPE_Kill;

	msg.Message.mn_ReplyPort = myport;

	if( (dllport = FindPort((unsigned char *)portname)) )
	{
		PutMsg(dllport, (struct Message *)&msg);
		/*WaitPort(myport);*/

		while(!(reply = (dll_tMessage *)GetMsg(myport)))
		{
			Delay(2);

			if(FindPort( (unsigned char *)portname) != dllport)
				break;
		}
	}

	DeleteMsgPort(myport);

	return (dllport ? 1 : 0);
}
#endif
