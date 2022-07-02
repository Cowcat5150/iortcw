/* DLL Startup function
 * This file gets linked in when the user does not define a main function
 * that is, if he wants to compile a dll
 */

#define __DLL_LIB_BUILD

//#include "dll.h"

#pragma pack(push,2)

#include <exec/exec.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <clib/alib_protos.h>

#if defined(__PPC__)

#include <exec/exec.h>
#include <exec/memory.h>

#if defined(__GNUC__)
#include <powerpc/powerpc_protos.h>
#else
#include <powerpc/powerpc.h>
#include <proto/powerpc.h>
#endif
#endif

#pragma pack(pop)

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "dll.h"

#if defined(__PPC__)

#undef CreateMsgPort
#undef DeleteMsgPort
#undef AddPort
#undef RemPort
#undef GetMsg
#undef ReplyMsg
#undef FindPort
#undef WaitPort
#undef PutMsg

#define CreateMsgPort CreateMsgPortPPC
#define DeleteMsgPort DeleteMsgPortPPC
#define AddPort AddPortPPC
#define RemPort RemPortPPC
#define GetMsg GetMsgPPC
#define ReplyMsg ReplyMsgPPC
#define FindPort FindPortPPC
#define WaitPort WaitPortPPC
#define PutMsg PutMsgPPC

#endif

/*
* Only DLL's can export symbols so this function is defined here.
* Note that on the other hand normal executables *can* have a symbolimport table,
* so dllImportSymbols is defined elsewhere.
*/

void dllExportSymbol( dll_tSymbolQuery *sym )
{
	dll_tExportSymbol *symtable = DLL_ExportSymbols; // reference DLL's export symbol table

	if(!sym->SymbolPointer)
		return;		// Paranoia

	while(symtable->SymbolAddress) // End of table ??
	{
		if(strcmp(symtable->SymbolName,sym->SymbolName)==0)
		{
			//FIXME: Stackframe handling
			*sym->SymbolPointer = symtable->SymbolAddress;
			return;
		}

		symtable++;
	}

	//*sym->SymbolPointer = 0L; //Symbol not found
	*sym->SymbolPointer = NULL;
}

/* 
** The actual main function of a DLL
*/

int main( int argc, char **argv )
{
	#if defined(__PPC__)
	struct MsgPortPPC *myport;
	#else
	struct MsgPort	*myport;
	#endif

	char		*PortName;
	dll_tMessage	*msg;
	int		expunge = 0L;
	int		opencount = 0L;

	/*
	* If an argument was passed, use it as the port name,
	* otherwise use the program name
	*/

	if (argc > 1)
	{
		PortName = argv[1];
	}

	else
	{
		PortName = argv[0];
	}

	/*
	* Process symbol import table
	*/

	if( !dllImportSymbols() )
		return 0;

	/*
	* Call DLL specific constructor
	*/

	if( !DLL_Init() )
		return 0;

	/*
	* Create a (public) message port
	*/

	myport = CreateMsgPort();

	#if defined(__PPC__)
	myport->mp_Port.mp_Node.ln_Name = PortName;
	myport->mp_Port.mp_Node.ln_Pri = 0;
	#else
	myport->mp_Node.ln_Name = PortName;
	myport->mp_Node.ln_Pri = 0;
	#endif

	AddPort(myport);

	if ( !myport )
	{
		RemPort(myport);
		return 0;
	}

	//printf("Start 6 to the loop\n");

	/*
	** Loop until DLL expunges (that is if a CloseMessage leads to opencount==0)
	** and no pending Messages are left
	*/

	while( ( msg = (dll_tMessage *)GetMsg(myport) ) || (!expunge) )
	{
		if (msg)
		{
			switch(msg->dllMessageType)
			{
				case DLLMTYPE_Open:

					/*
					* Stack type checking should go here. Might be ommited for strictly
					* private DLLs, or when stack frame compatibility can be 100% assured.
					* FIXME: Not handled for now
					*/

					opencount++;

					if(opencount > 0)
						expunge = 0L;

					msg->dllMessageData.dllOpen.ErrorCode = DLLERR_NoError;

					break;

				case DLLMTYPE_Close:

					opencount--;

					if(opencount <= 0L)    // <0 ????
						expunge = 1L;

					break;

				case DLLMTYPE_SymbolQuery:

					dllExportSymbol(&msg->dllMessageData.dllSymbolQuery);

					//printf("Symbol Query for %s : %p\n",msg->dllMessageData.dllSymbolQuery.SymbolName, 
						//*msg->dllMessageData.dllSymbolQuery.SymbolPointer);

					break;
					
				case DLLMTYPE_Kill:

					expunge = 1L;
					break;
			}

			/*
			* Send the message back
			*/
			ReplyMsg((struct Message *)msg);
		}

		/*
		* Wait for messages to pop up
		* Note that if the DLL is expunged it doesn't wait anymore,
		* but it still processes all pending messages (including open messages
		* which can disable the expunge flag).
		* FIXME: Is this multithread safe ??
		*/

		if( !expunge )
			WaitPort(myport);
	}

	/*
	* Delete public port
	*/

	RemPort(myport);
	DeleteMsgPort(myport);

	/*
	* Call DLL specific destructor
	*/
	DLL_DeInit();

	return 0;
}

