#include <stdlib.h> // strtol
#include <unistd.h>
//#include <stdio.h>
#include <nds.h>
#include <string>
#include <string.h>
#include <limits.h> // PATH_MAX
/*#include <nds/ndstypes.h>
#include <nds/fifocommon.h>
#include <nds/arm9/console.h>
#include <nds/debug.h>*/
#include <fat.h>
#include "lzss.h"
#include <easysave/ini.hpp>
#include "hex.h"
#include "cheat_engine.h"
#include "configuration.h"
#include "conf_sd.h"
#include "nitrofs.h"
#include "locations.h"

extern std::string patchOffsetCacheFilePath;
extern std::string srParamsFilePath;

extern u8 lz77ImageBuffer[0x10000];

static off_t getFileSize(const char* path) {
	FILE* fp = fopen(path, "rb");
	off_t fsize = 0;
	if (fp) {
		fseek(fp, 0, SEEK_END);
		fsize = ftell(fp);
		if (!fsize) fsize = 0;
		fclose(fp);
	}
	return fsize;
}

extern std::string ReplaceAll(std::string str, const std::string& from, const std::string& to);
extern bool extention(const std::string& filename, const char* ext);

static void load_conf(configuration* conf, const char* fn) {
	easysave::ini config_file(fn);

	// Debug
	conf->debug = (bool)strtol(config_file.fetch("NDS-BOOTSTRAP", "DEBUG").c_str(), NULL, 0);

	// NDS path
	conf->ndsPath = strdup(config_file.fetch("NDS-BOOTSTRAP", "NDS_PATH").c_str());

	// SAV path
	conf->savPath = strdup(config_file.fetch("NDS-BOOTSTRAP", "SAV_PATH").c_str());

	// Early SDK2 Donor NDS path
	conf->donorE2Path = strdup(config_file.fetch("NDS-BOOTSTRAP", "DONORE2_NDS_PATH").c_str());

	// Late SDK2 Donor NDS path
	conf->donor2Path = strdup(config_file.fetch("NDS-BOOTSTRAP", "DONOR2_NDS_PATH").c_str());

	// SDK3-4 Donor NDS path
	conf->donor3Path = strdup(config_file.fetch("NDS-BOOTSTRAP", "DONOR3_NDS_PATH").c_str());

	// SDK5 Donor NDS path
	conf->donorPath = strdup(config_file.fetch("NDS-BOOTSTRAP", "DONOR_NDS_PATH").c_str());

	// SDK5 (TWL) Donor NDS path
	conf->donorTwlPath = strdup(config_file.fetch("NDS-BOOTSTRAP", "DONORTWL_NDS_PATH").c_str());

	// AP-patch path
	conf->apPatchPath = strdup(config_file.fetch("NDS-BOOTSTRAP", "AP_FIX_PATH").c_str());

	// Language
	conf->language = strtol(config_file.fetch("NDS-BOOTSTRAP", "LANGUAGE").c_str(), NULL, 0);

	// Donor SDK version
	conf->donorSdkVer = strtol(config_file.fetch("NDS-BOOTSTRAP", "DONOR_SDK_VER").c_str(), NULL, 0);

	// Patch MPU region
	conf->patchMpuRegion = strtol(config_file.fetch("NDS-BOOTSTRAP", "PATCH_MPU_REGION").c_str(), NULL, 0);

	// Patch MPU size
	conf->patchMpuSize = strtol(config_file.fetch("NDS-BOOTSTRAP", "PATCH_MPU_SIZE").c_str(), NULL, 0);

	// Card engine (arm9) cached
	conf->ceCached = (bool)strtol(config_file.fetch("NDS-BOOTSTRAP", "CARDENGINE_CACHED").c_str(), NULL, 0);

	// Force sleep patch
	conf->forceSleepPatch = (bool)strtol(config_file.fetch("NDS-BOOTSTRAP", "FORCE_SLEEP_PATCH").c_str(), NULL, 0);

	// Logging
	conf->logging = (bool)strtol(config_file.fetch("NDS-BOOTSTRAP", "LOGGING").c_str(), NULL, 0);

	// Backlight mode
	conf->backlightMode = strtol(config_file.fetch("NDS-BOOTSTRAP", "BACKLIGHT_MODE").c_str(), NULL, 0);

	// Boost CPU
	conf->boostCpu = (bool)strtol(config_file.fetch("NDS-BOOTSTRAP", "BOOST_CPU").c_str(), NULL, 0);

	// Boost VRAM
	conf->boostVram = (bool)strtol(config_file.fetch("NDS-BOOTSTRAP", "BOOST_VRAM").c_str(), NULL, 0);
}

int loadFromSD(configuration* conf, const char *bootstrapPath) {
	if (!fatInitDefault()) {
		consoleDemoInit();
		printf("FAT init failed!\n");
		return -1;
	}
	nocashMessage("fatInitDefault");

	if ((access("fat:/", F_OK) != 0) && (access("sd:/", F_OK) == 0)) {
		consoleDemoInit();
		printf("This edition of nds-bootstrap:\n");
		printf("B4DS, can only be used\n");
		printf("on a flashcard.\n");
		return -1;
	}
	
	load_conf(conf, "fat:/_nds/nds-bootstrap.ini");
	mkdir("fat:/_nds", 0777);
	mkdir("fat:/_nds/nds-bootstrap", 0777);
	mkdir("fat:/_nds/nds-bootstrap/B4DS-patchOffsetCache", 0777);

	if (!nitroFSInit(bootstrapPath)) {
		consoleDemoInit();
		printf("nitroFSInit failed!\n");
		return -1;
	}
	
	// Load ce7 binary
	FILE* cebin = fopen("nitro:/cardengine_arm7.bin", "rb");
	if (cebin) {
		fread((void*)CARDENGINE_ARM7_LOCATION, 1, 0x800, cebin);
	}
	fclose(cebin);
    
	// Load ce9 binary 1
	cebin = fopen("nitro:/cardengine_arm9.bin", "rb");
	if (cebin) {
		fread((void*)CARDENGINE_ARM9_LOCATION_BUFFERED1, 1, 0x5000, cebin);
	}
	fclose(cebin);
    
	// Load ce9 binary 2
	cebin = fopen("nitro:/cardengine_arm9_8kb.bin", "rb");
	if (cebin) {
		fread((void*)CARDENGINE_ARM9_LOCATION_BUFFERED2, 1, 0x4000, cebin);
	}
	fclose(cebin);
    
	conf->romSize = getFileSize(conf->ndsPath);
	conf->saveSize = getFileSize(conf->savPath);
	conf->apPatchSize = getFileSize(conf->apPatchPath);

	FILE* bootstrapImages = fopen("nitro:/bootloader_images.lz77", "rb");
	if (bootstrapImages) {
		fread(lz77ImageBuffer, 1, 0x8000, bootstrapImages);
		LZ77_Decompress(lz77ImageBuffer, (u8*)IMAGES_LOCATION);
	}
	fclose(bootstrapImages);

	const char *typeToReplace = ".nds";
	if (extention(conf->ndsPath, ".dsi")) {
		typeToReplace = ".dsi";
	} else if (extention(conf->ndsPath, ".ids")) {
		typeToReplace = ".ids";
	} else if (extention(conf->ndsPath, ".srl")) {
		typeToReplace = ".srl";
	} else if (extention(conf->ndsPath, ".app")) {
		typeToReplace = ".app";
	}

	std::string romFilename = ReplaceAll(conf->ndsPath, typeToReplace, ".bin");
	const size_t last_slash_idx = romFilename.find_last_of("/");
	if (std::string::npos != last_slash_idx)
	{
		romFilename.erase(0, last_slash_idx + 1);
	}

	srParamsFilePath = "fat:/_nds/nds-bootstrap/B4DS-softResetParams.bin";
	
	if (access(srParamsFilePath.c_str(), F_OK) != 0) {
		u32 buffer = 0xFFFFFFFF;

		FILE* srParamsFile = fopen(srParamsFilePath.c_str(), "wb");
		fwrite(&buffer, sizeof(u32), 1, srParamsFile);
		fclose(srParamsFile);
	}

	patchOffsetCacheFilePath = "fat:/_nds/nds-bootstrap/B4DS-patchOffsetCache/"+romFilename;
	
	if (access(patchOffsetCacheFilePath.c_str(), F_OK) != 0) {
		char buffer[0x200] = {0};

		FILE* patchOffsetCacheFile = fopen(patchOffsetCacheFilePath.c_str(), "wb");
		fwrite(buffer, 1, sizeof(buffer), patchOffsetCacheFile);
		fclose(patchOffsetCacheFile);
	}

	return 0;
}
