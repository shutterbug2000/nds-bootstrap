#ifndef LOAD_CRT0_H
#define LOAD_CRT0_H

#include <nds/ndstypes.h>
#include "cardengine_header_arm7.h"
#include "cardengine_header_arm9.h"

typedef struct loadCrt0 {
    u32 _start;
    u32 storedFileCluster;
    u32 initDisc;
    u16 gameOnFlashcard;
    u16 saveOnFlashcard;
    u32 donorOnFlashcard;
    u32 dldiOffset;
    u32 dsiSD;
    u32 saveFileCluster;
	u32 donorFileE2Cluster;
	u32 donorFile2Cluster;
	u32 donorFile3Cluster;
	u32 donorFileCluster;
	u32 donorFileTwlCluster;
    u32 gbaFileCluster;
    u32 gbaSaveFileCluster;
    u32 romSize;
    u32 saveSize;
    u32 gbaRomSize;
    u32 gbaSaveSize;
    u32 wideCheatFileCluster;
    u32 wideCheatSize;
    u32 apPatchFileCluster;
    u32 apPatchSize;
    u32 cheatFileCluster;
    u32 cheatSize;
    u32 patchOffsetCacheFileCluster;
    u32 cacheFatTable;
    u32 fatTableFileCluster;
    u32 ramDumpCluster;
	u32 srParamsFileCluster;
    u32 language; //u8
    u32 dsiMode; // SDK 5
    u32 donorSdkVer;
    u32 patchMpuRegion;
    u32 patchMpuSize;
    u32 ceCached; // SDK 1-4
    u32 cacheBlockSize;
    u32 extendedMemory;
    u32 consoleModel;
    u32 romRead_LED;
    u32 dmaRomRead_LED;
    u32 boostVram;
    u32 soundFreq;
    u32 forceSleepPatch;
	u32 volumeFix;
    u32 preciseVolumeControl;
    u32 macroMode;
    u32 logging;
} __attribute__ ((__packed__)) loadCrt0;

#endif // LOAD_CRT0_H
