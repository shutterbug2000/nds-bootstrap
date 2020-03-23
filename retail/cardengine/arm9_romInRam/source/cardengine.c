/*
    NitroHax -- Cheat tool for the Nintendo DS
    Copyright (C) 2008  Michael "Chishm" Chisholm

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <string.h>
#include <nds/ndstypes.h>
#include <nds/arm9/exceptions.h>
#include <nds/arm9/cache.h>
#include <nds/arm9/video.h>
#include <nds/system.h>
#include <nds/dma.h>
#include <nds/interrupts.h>
#include <nds/ipc.h>
#include <nds/fifomessages.h>
#include <nds/memory.h> // tNDSHeader
#include "tonccpy.h"
#include "hex.h"
#include "nds_header.h"
#include "module_params.h"
#include "cardengine.h"
#include "locations.h"
#include "cardengine_header_arm9.h"

#define THRESHOLD_CACHE_FLUSH 0x500

#define END_FLAG   0
#define BUSY_FLAG   4  

//extern void user_exception(void);

extern cardengineArm9* volatile ce9;

vu32* volatile sharedAddr = (vu32*)CARDENGINE_SHARED_ADDRESS;

static tNDSHeader* ndsHeader = (tNDSHeader*)NDS_HEADER;

static u32 romLocation = ROM_LOCATION;

static bool flagsSet = false;
static bool isDma = false;
static bool dmaLed = false;

void myIrqHandlerDMA(void);

void SetBrightness(u8 screen, s8 bright) {
	u16 mode = 1 << 14;

	if (bright < 0) {
		mode = 2 << 14;
		bright = -bright;
	}
	if (bright > 31) {
		bright = 31;
	}
	*(u16*)(0x0400006C + (0x1000 * screen)) = bright + mode;
}

// Alternative to swiWaitForVBlank()
static void waitFrames(int count) {
	for (int i = 0; i < count; i++) {
		while (REG_VCOUNT != 191);
		while (REG_VCOUNT == 191);
	}
}

static int readCount = 0;
/*static bool sleepMsEnabled = false;

static void sleepMs(int ms) {
	extern void callSleepThumb(int ms);

	if (readCount >= 100) {
		sleepMsEnabled = true;
	}

	if (!sleepMsEnabled) return;

    if(ce9->patches->sleepRef) {
        volatile void (*sleepRef)(int ms) = (volatile void*)ce9->patches->sleepRef;
        (*sleepRef)(ms);
    } else if(ce9->thumbPatches->sleepRef) {
        callSleepThumb(ms);
    }
}*/

static void waitForArm7(void) {
	IPC_SendSync(0x4);
	while (sharedAddr[3] != (vu32)0);
}

static inline
/*! \fn void ndmaCopyWordsAsynch(uint8 channel, const void* src, void* dest, uint32 size)
\brief copies from source to destination on one of the 4 available channels in half words.  
This function returns immediately after starting the transfer.
\param channel the dma channel to use (0 - 3).  
\param src the source to copy from
\param dest the destination to copy to
\param size the size in bytes of the data to copy.  Will be truncated to the nearest word (4 bytes)
*/
void ndmaCopyWordsAsynch(uint8 ndmaSlot, const void* src, void* dest, uint32 size) {
	*(u32*)(0x4004104+(ndmaSlot*0x1C)) = src;
	*(u32*)(0x4004108+(ndmaSlot*0x1C)) = dest;
	
	*(u32*)(0x4004110+(ndmaSlot*0x1C)) = size/4;	
	
    *(u32*)(0x4004114+(ndmaSlot*0x1C)) = 0x1;
	
	*(u32*)(0x400411C+(ndmaSlot*0x1C)) = 0x90070000;
}

/*static inline 
void ndmaCopyWordsAsynchTimer1(uint8 ndmaSlot, const void* src, void* dest, uint32 size) {
	*(u32*)(0x4004104+(ndmaSlot*0x1C)) = src;
	*(u32*)(0x4004108+(ndmaSlot*0x1C)) = dest;

    *(u32*)(0x400410C+(ndmaSlot*0x1C)) = size/4;
	
	*(u32*)(0x4004110+(ndmaSlot*0x1C)) = 0x80;
	
    *(u32*)(0x4004114+(ndmaSlot*0x1C)) = 0x1;
	
	*(u32*)(0x400411C+(ndmaSlot*0x1C)) = 0x81070000;
} */

static inline 
bool ndmaBusy(uint8 ndmaSlot) {
	return	*(u32*)(0x400411C+(ndmaSlot*0x1C)) & BIT(31) == 0x80000000;
}

static bool IPC_SYNC_hooked = false;
static void hookIPC_SYNC(void) {
    if (!IPC_SYNC_hooked) {
        u32* ipcSyncHandler = ce9->irqTable + 16;
        ce9->intr_ipc_orig_return = *ipcSyncHandler;
        *ipcSyncHandler = ce9->patches->ipcSyncHandlerRef;
        IPC_SYNC_hooked = true;
    }
}

static void enableIPC_SYNC(void) {
	if (IPC_SYNC_hooked && !(REG_IE & IRQ_IPC_SYNC)) {
		REG_IE |= IRQ_IPC_SYNC;
	}
}


static void clearIcache (void) {
      // Seems to have no effect
      // disable interrupt
      /*int oldIME = enterCriticalSection();
      IC_InvalidateAll();
      // restore interrupt
      leaveCriticalSection(oldIME);*/
}

void endCardReadDma() {
    if(ce9->patches->cardEndReadDmaRef) {
        volatile void (*cardEndReadDmaRef)() = ce9->patches->cardEndReadDmaRef;
        (*cardEndReadDmaRef)();
    } else if(ce9->thumbPatches->cardEndReadDmaRef) {
        callEndReadDmaThumb();
    }    
}

void cardSetDma(void) {
	vu32* volatile cardStruct = ce9->cardStruct0;

    disableIrqMask(IRQ_CARD);
    disableIrqMask(IRQ_CARD_LINE);

	enableIPC_SYNC();

	u32 src = cardStruct[0];
	u8* dst = (u8*)(cardStruct[1]);
	u32 len = cardStruct[2];
    u32 dma = cardStruct[3]; // dma channel     

  	u32 len2 = len;
  	if (len2 > 512) {
  		len2 -= src % 4;
  		len2 -= len2 % 32;
  	}

	// Copy via dma
	int oldIME = 0;
	if (ce9->extendedMemory && !ce9->dsiMode) {
		oldIME = enterCriticalSection();
		REG_SCFG_EXT += 0xC000;
	}
	ndmaCopyWordsAsynch(0, (u8*)((romLocation-0x4000-ndsHeader->arm9binarySize)+src), dst, len2);
	while (ndmaBusy(0));
	if (ce9->extendedMemory && !ce9->dsiMode) {
		REG_SCFG_EXT -= 0xC000;
		leaveCriticalSection(oldIME);
	}
	endCardReadDma();
}

//Currently used for NSMBDS romhacks
void __attribute__((target("arm"))) debug8mbMpuFix(){
	asm("MOV R0,#0\n\tmcr p15, 0, r0, C6,C2,0");
}

bool isNotTcm(u32 address, u32 len) {
    u32 base = (getDtcmBase()>>12) << 12;
    return    // test data not in ITCM
    address > 0x02000000
    // test data not in DTCM
    && (address < base || address> base+0x4000)
    && (address+len < base || address+len> base+0x4000);     
}  

u32 cardReadDma() {
	vu32* volatile cardStruct = ce9->cardStruct0;
    
	u32 src = cardStruct[0];
	u8* dst = (u8*)(cardStruct[1]);
	u32 len = cardStruct[2];
    u32 dma = cardStruct[3]; // dma channel

    if(dma >= 0 
        && dma <= 3 
        //&& func != NULL
        && len > 0
        && !(((int)dst) & 3)
        && isNotTcm(dst, len)
        // check 512 bytes page alignement 
        && !(((int)len) & 511)
        && !(((int)src) & 511)
	) {
		dmaLed = true;

        if(ce9->patches->cardEndReadDmaRef || ce9->thumbPatches->cardEndReadDmaRef)
		{
			isDma = true;
			// new dma method

            cacheFlush();

            cardSetDma();

            return true;
		} else {
			isDma = false;
			dma=4;
            clearIcache();
		}
    } else {
		dmaLed = false;
        isDma = false;
        dma=4;
        clearIcache();
    }

    return 0;
}

static int counter=0;
int cardReadPDash(u32* cacheStruct, u32 src, u8* dst, u32 len) {
#ifndef DLDI
    dmaLed = true;
#endif

	vu32* volatile cardStruct = (vu32* volatile)ce9->cardStruct0;

    cardStruct[0] = src;
    cardStruct[1] = (vu32)dst;
    cardStruct[2] = len;

    cardRead(cacheStruct, dst, src, len);

    counter++;
	return counter;
}

int cardRead(u32* cacheStruct, u8* dst0, u32 src0, u32 len0) {
	//nocashMessage("\narm9 cardRead\n");
	if (!flagsSet) {
		if (isSdk5(ce9->moduleParams)) {
			ndsHeader = (tNDSHeader*)NDS_HEADER_SDK5;
		} else {
			debug8mbMpuFix();
		}
		if (ce9->dsiMode || (!ce9->extendedMemory && isSdk5(ce9->moduleParams))) {
			romLocation = ROM_SDK5_LOCATION;
		} else if (ce9->extendedMemory) {
			ndsHeader = (tNDSHeader*)NDS_HEADER_4MB;
			romLocation = ROM_LOCATION_EXT;
		}

		//if (ce9->enableExceptionHandler && ce9==CARDENGINE_ARM9_LOCATION) {
			//exceptionStack = (u32)EXCEPTION_STACK_LOCATION;
			//setExceptionHandler(user_exception);
		//}

		flagsSet = true;
	}
	
	enableIPC_SYNC();

	vu32* volatile cardStruct = (vu32* volatile)ce9->cardStruct0;

	u32 src = (isSdk5(ce9->moduleParams) ? src0 : cardStruct[0]);
	u8* dst = (isSdk5(ce9->moduleParams) ? dst0 : (u8*)(cardStruct[1]));
	u32 len = (isSdk5(ce9->moduleParams) ? len0 : cardStruct[2]);

	#ifdef DEBUG
	u32 commandRead;

	// send a log command for debug purpose
	// -------------------------------------
	commandRead = 0x026ff800;

	sharedAddr[0] = dst;
	sharedAddr[1] = len;
	sharedAddr[2] = src;
	sharedAddr[3] = commandRead;

	waitForArm7();
	// -------------------------------------*/
	#endif

	readCount++;

	if (src == 0) {
		// If ROM read location is 0, do not proceed.
		return 0;
	}

	// Fix reads below 0x8000
	if (src <= 0x8000){
		src = 0x8000 + (src & 0x1FF);
	}

	int oldIME = 0;
	if (ce9->extendedMemory && !ce9->dsiMode) {
		oldIME = enterCriticalSection();
		REG_SCFG_EXT += 0xC000;
	}
	tonccpy(dst, (u8*)((romLocation-0x4000-ndsHeader->arm9binarySize)+src),len);
	if (ce9->extendedMemory && !ce9->dsiMode) {
		REG_SCFG_EXT -= 0xC000;
		leaveCriticalSection(oldIME);
	}

    isDma=false;

	return 0; 
}

void cardPullOut(void) {
	/*if (*(vu32*)(0x027FFB30) != 0) {
		/*volatile int (*terminateForPullOutRef)(u32*) = *ce9->patches->terminateForPullOutRef;
        (*terminateForPullOutRef);
		sharedAddr[3] = 0x5245424F;
		waitForArm7();
	}*/
}

u32 nandRead(void* memory,void* flash,u32 len,u32 dma) {
	if (ce9->saveOnFlashcard) {
		return 0;
	}

    // Send a command to the ARM7 to read the nand save
	u32 commandNandRead = 0x025FFC01;

	// Write the command
	sharedAddr[0] = memory;
	sharedAddr[1] = len;
	sharedAddr[2] = flash;
	sharedAddr[3] = commandNandRead;

	waitForArm7();
    return 0; 
}

u32 nandWrite(void* memory,void* flash,u32 len,u32 dma) {
	if (ce9->saveOnFlashcard) {
		return 0;
	}

	// Send a command to the ARM7 to write the nand save
	u32 commandNandWrite = 0x025FFC02;

	// Write the command
	sharedAddr[0] = memory;
	sharedAddr[1] = len;
	sharedAddr[2] = flash;
	sharedAddr[3] = commandNandWrite;

	waitForArm7();
    return 0; 
}

u32 slot2Read(u8* dst, u32 src, u32 len, u32 dma) {
	// Send a command to the ARM7 to read the GBA ROM
	/*u32 commandRead = 0x025FBC01;

	// Write the command
	sharedAddr[0] = (vu32)dst;
	sharedAddr[1] = len;
	sharedAddr[2] = src;
	sharedAddr[3] = commandRead;

	waitForArm7();*/
    return 0; 
}

//---------------------------------------------------------------------------------
void myIrqHandlerIPC(void) {
//---------------------------------------------------------------------------------
	#ifdef DEBUG		
	nocashMessage("myIrqHandlerIPC");
	#endif	

	if (sharedAddr[4] == 0x57534352) {
		enterCriticalSection();
		// Make screens white
		SetBrightness(0, 31);
		SetBrightness(1, 31);

		while (1);
	}
}

void reset(u32 param) {
	if (ce9->consoleModel < 2) {
		// Make screens white
		SetBrightness(0, 31);
		SetBrightness(1, 31);
		waitFrames(5);	// Wait for DSi screens to stabilize
	}
	enterCriticalSection();
	*(u32*)RESET_PARAM = param;
	sharedAddr[3] = 0x52534554;
	while (1);
}

u32 myIrqEnable(u32 irq) {	
	int oldIME = enterCriticalSection();

	#ifdef DEBUG
	nocashMessage("myIrqEnable\n");
	#endif

	hookIPC_SYNC();

	u32 irq_before = REG_IE | IRQ_IPC_SYNC;		
	irq |= IRQ_IPC_SYNC;
	REG_IPC_SYNC |= IPC_SYNC_IRQ_ENABLE;

	REG_IE |= irq;
	leaveCriticalSection(oldIME);
	return irq_before;
}
