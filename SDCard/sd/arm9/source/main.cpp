#include <nds.h>
#include <nds/fifocommon.h>
#include <stdio.h>
#include <fat.h>
#include "fat_ext.h"
#include <sys/stat.h>
#include <limits.h>
#include <nds/disc_io.h>

#include <string.h>
#include <unistd.h>

#include "ndsheaderbanner.h"
#include "nds_loader_arm9.h"
#include "nitrofs.h"
#include "tonccpy.h"

#include "cheat.h"
#include "inifile.h"
#include "fileCopy.h"
#include "perGameSettings.h"

#include "twlClockExcludeMap.h"
#include "dmaExcludeMap.h"
#include "asyncReadExcludeMap.h"
#include "donorMap.h"
#include "saveMap.h"
#include "ROMList.h"

using namespace std;

static int language = -1;
static int region = -2;
static bool cacheFatTable = false;

static bool bootstrapFile = false;
static bool widescreenLoaded = false;

static bool consoleInited = false;

int consoleModel = 0; // 0: Retail DSi

//---------------------------------------------------------------------------------
void stop (void) {
//---------------------------------------------------------------------------------
	while (1) {
		swiWaitForVBlank();
	}
}

char filePath[PATH_MAX];

//---------------------------------------------------------------------------------
void doPause() {
//---------------------------------------------------------------------------------
	iprintf("Press start...\n");
	while(1) {
		scanKeys();
		if(keysDown() & KEY_START)
			break;
		swiWaitForVBlank();
	}
	scanKeys();
}

bool extention(const std::string& filename, const char* ext) {
	if(strcasecmp(filename.c_str() + filename.size() - strlen(ext), ext)) {
		return false;
	} else {
		return true;
	}
}

/**
 * Remove trailing slashes from a pathname, if present.
 * @param path Pathname to modify.
 */
void RemoveTrailingSlashes(std::string& path)
{
	while (!path.empty() && path[path.size()-1] == '/') {
		path.resize(path.size()-1);
	}
}

/**
 * Disable TWL clock speed for a specific game.
 */
bool setClockSpeed(int current, const char* filename) {
	FILE *f_nds_file = fopen(filename, "rb");

	char game_TID[5];
	fseek(f_nds_file, 0xC, SEEK_SET);
	fread(game_TID, 1, 4, f_nds_file);
	game_TID[4] = 0;
	fclose(f_nds_file);

	// TODO: If the list gets large enough, switch to bsearch().
	for (unsigned int i = 0; i < sizeof(twlClockExcludeList)/sizeof(twlClockExcludeList[0]); i++) {
		if (memcmp(game_TID, twlClockExcludeList[i], 3) == 0) {
			// Found match
			return false;
		}
	}

	return current == -1 ? false : current;
}

/**
 * Disable card read DMA for a specific game.
 */
bool setCardReadDMA(int current, const char* filename) {
	FILE *f_nds_file = fopen(filename, "rb");

	char game_TID[5];
	fseek(f_nds_file, 0xC, SEEK_SET);
	fread(game_TID, 1, 4, f_nds_file);
	game_TID[4] = 0;
	fclose(f_nds_file);

	// TODO: If the list gets large enough, switch to bsearch().
	for (unsigned int i = 0; i < sizeof(cardReadDMAExcludeList)/sizeof(cardReadDMAExcludeList[0]); i++) {
		if (memcmp(game_TID, cardReadDMAExcludeList[i], 3) == 0) {
			// Found match
			return false;
		}
	}

	return current == -1 ? true : current;
}

/**
 * Disable asynch card read for a specific game.
 */
bool setAsyncCardRead(int current, const char* filename) {
	FILE *f_nds_file = fopen(filename, "rb");

	char game_TID[5];
	fseek(f_nds_file, 0xC, SEEK_SET);
	fread(game_TID, 1, 4, f_nds_file);
	game_TID[4] = 0;
	fclose(f_nds_file);

	// TODO: If the list gets large enough, switch to bsearch().
	for (unsigned int i = 0; i < sizeof(asyncReadExcludeList)/sizeof(asyncReadExcludeList[0]); i++) {
		if (memcmp(game_TID, asyncReadExcludeList[i], 3) == 0) {
			// Found match
			return false;
		}
	}

	return current == -1 ? false : current;
}

/**
 * Set donor SDK version for a specific game.
 */
int SetDonorSDK(const char* filename) {
	FILE *f_nds_file = fopen(filename, "rb");

	char game_TID[5];
	fseek(f_nds_file, 0xC, SEEK_SET);
	fread(game_TID, 1, 4, f_nds_file);
	game_TID[4] = 0;
	game_TID[3] = 0;
	fclose(f_nds_file);
	
	for (auto i : donorMap) {
		if (i.first == 5 && game_TID[0] == 'V')
			return 5;

		if (i.second.find(game_TID) != i.second.cend())
			return i.first;
	}

	return 0;
}

/**
 * Fix AP for some games.
 */
std::string setApFix(const char *filename) {
	bool useTwlmPath = (access("sd:/_nds/TWiLightMenu/extras/apfix.pck", F_OK) == 0);

	char game_TID[5];
	u16 headerCRC16 = 0;

	bool ipsFound = false;
	bool cheatVer = true;
	char ipsPath[256];
	snprintf(ipsPath, sizeof(ipsPath), "sd:/_nds/%s/apfix/%s.ips", (useTwlmPath ? "TWiLightMenu/extras" : "ntr-forwarder"), filename);
	ipsFound = (access(ipsPath, F_OK) == 0);
	if (!ipsFound) {
		snprintf(ipsPath, sizeof(ipsPath), "sd:/_nds/%s/apfix/%s.bin", (useTwlmPath ? "TWiLightMenu/extras" : "ntr-forwarder"), filename);
		ipsFound = (access(ipsPath, F_OK) == 0);
	} else {
		cheatVer = false;
	}

	if (!ipsFound) {
		FILE *f_nds_file = fopen(filename, "rb");

		fseek(f_nds_file, offsetof(sNDSHeaderExt, gameCode), SEEK_SET);
		fread(game_TID, 1, 4, f_nds_file);
		fseek(f_nds_file, offsetof(sNDSHeaderExt, headerCRC16), SEEK_SET);
		fread(&headerCRC16, sizeof(u16), 1, f_nds_file);
		fclose(f_nds_file);
		game_TID[4] = 0;

		snprintf(ipsPath, sizeof(ipsPath), "sd:/_nds/%s/apfix/%s-%X.ips", (useTwlmPath ? "TWiLightMenu/extras" : "ntr-forwarder"), game_TID, headerCRC16);
		ipsFound = (access(ipsPath, F_OK) == 0);
		if (!ipsFound) {
			snprintf(ipsPath, sizeof(ipsPath), "sd:/_nds/%s/apfix/%s-%X.bin", (useTwlmPath ? "TWiLightMenu/extras" : "ntr-forwarder"), game_TID, headerCRC16);
			ipsFound = (access(ipsPath, F_OK) == 0);
		} else {
			cheatVer = false;
		}
	}

	if (ipsFound) {
		return ipsPath;
	}

	FILE *file = fopen(useTwlmPath ? "sd:/_nds/TWiLightMenu/extras/apfix.pck" : "sd:/_nds/ntr-forwarder/apfix.pck", "rb");
	if (file) {
		char buf[5] = {0};
		fread(buf, 1, 4, file);
		if (strcmp(buf, ".PCK") != 0) // Invalid file
			return "";

		u32 fileCount;
		fread(&fileCount, 1, sizeof(fileCount), file);

		u32 offset = 0, size = 0;

		// Try binary search for the game
		int left = 0;
		int right = fileCount;

		while (left <= right) {
			int mid = left + ((right - left) / 2);
			fseek(file, 16 + mid * 16, SEEK_SET);
			fread(buf, 1, 4, file);
			int cmp = strcmp(buf,  game_TID);
			if (cmp == 0) { // TID matches, check CRC
				u16 crc;
				fread(&crc, 1, sizeof(crc), file);

				if (crc == headerCRC16) { // CRC matches
					fread(&offset, 1, sizeof(offset), file);
					fread(&size, 1, sizeof(size), file);
					cheatVer = fgetc(file) & 1;
					break;
				} else if (crc < headerCRC16) {
					left = mid + 1;
				} else {
					right = mid - 1;
				}
			} else if (cmp < 0) {
				left = mid + 1;
			} else {
				right = mid - 1;
			}
		}

		if (offset > 0 && size > 0) {
			fseek(file, offset, SEEK_SET);
			u8 *buffer = new u8[size];
			fread(buffer, 1, size, file);

			mkdir("sd:/_nds/nds-bootstrap", 0777);
			snprintf(ipsPath, sizeof(ipsPath), "sd:/_nds/nds-bootstrap/apFix%s", cheatVer ? "Cheat.bin" : ".ips");
			FILE *out = fopen(ipsPath, "wb");
			if(out) {
				fwrite(buffer, 1, size, out);
				fclose(out);
			}
			delete[] buffer;
			fclose(file);
			return ipsPath;
		}

		fclose(file);
	}

	return "";
}

void setAutoload(const char *resetTid) {
	u8 *autoloadParams = (u8 *)0x02000300;

	toncset32(autoloadParams, 0x434E4C54, 1); // 'TLNC'
	((vu8 *)autoloadParams)[4] = 0x01;
	((vu8 *)autoloadParams)[5] = 0x18; // Length of data

	// Old TID, can be 0
	toncset(autoloadParams + 8, 0, 8);

	// New TID
	tonccpy(autoloadParams + 16, resetTid, 4);
	toncset32(autoloadParams + 20, 0x00030000 | resetTid[4], 1);

	toncset32(autoloadParams + 24, 0x00000017, 1); // Flags
	toncset32(autoloadParams + 28, 0x00000000, 1);

	// CRC16
	u16 crc16 = swiCRC16(0xFFFF, autoloadParams + 8, 0x18);
	tonccpy(autoloadParams + 6, &crc16, 2);
}

/**
 * Enable widescreen for some games.
 */
void SetWidescreen(const char *filename, bool isHomebrew, const char *resetTid, bool force) {
	bool useTwlmPath = (access("sd:/_nds/TWiLightMenu/extras/widescreen.pck", F_OK) == 0);

	const char* wideCheatDataPath = "sd:/_nds/nds-bootstrap/wideCheatData.bin";
	remove(wideCheatDataPath);

	if (isHomebrew) {
		if (access("sd:/luma/sysmodules/TwlBg.cxi", F_OK) == 0) {
			rename("sd:/luma/sysmodules/TwlBg.cxi", "sd:/_nds/ntr-forwarder/TwlBg.cxi.bak");
		}
		if (rename("sd:/_nds/TWiLightMenu/TwlBg/Widescreen.cxi", "sd:/luma/sysmodules/TwlBg.cxi") == 0
		 || rename("sd:/_nds/ntr-forwarder/Widescreen.cxi", "sd:/luma/sysmodules/TwlBg.cxi") == 0) {
			CIniFile ntrforwarderini("sd:/_nds/ntr_forwarder.ini");
			ntrforwarderini.SetInt("NTR-FORWARDER", "WIDESCREEN_LOADED", true);
			ntrforwarderini.SaveIniFile("sd:/_nds/ntr_forwarder.ini");

			setAutoload(resetTid);
			DC_FlushAll();
			fifoSendValue32(FIFO_USER_01, 1);
			stop();
		}
		return;
	}

	char tid[5];
	u16 crc16;

	FILE *f_nds_file = fopen(filename, "rb");
	fseek(f_nds_file, offsetof(sNDSHeaderExt, gameCode), SEEK_SET);
	fread(tid, 1, 4, f_nds_file);
	fseek(f_nds_file, offsetof(sNDSHeaderExt, headerCRC16), SEEK_SET);
	fread(&crc16, sizeof(u16), 1, f_nds_file);
	fclose(f_nds_file);
	tid[4] = 0;

	bool wideCheatFound = false;
	char wideBinPath[256];
	snprintf(wideBinPath, sizeof(wideBinPath), "sd:/_nds/%s/widescreen/%s.bin", (useTwlmPath ? "TWiLightMenu/extras" : "ntr-forwarder"), filename);
	wideCheatFound = (access(wideBinPath, F_OK) == 0);

	if (!wideCheatFound) {
		snprintf(wideBinPath, sizeof(wideBinPath), "sd:/_nds/%s/widescreen/%s-%X.bin", (useTwlmPath ? "TWiLightMenu/extras" : "ntr-forwarder"), tid, crc16);
		wideCheatFound = (access(wideBinPath, F_OK) == 0);
	}

	mkdir("sd:/_nds", 0777);
	mkdir("sd:/_nds/nds-bootstrap", 0777);

	if (wideCheatFound) {
		if (fcopy(wideBinPath, wideCheatDataPath) != 0) {
			remove(wideCheatDataPath);
			consoleDemoInit();
			iprintf("Failed to copy widescreen\ncode for the game.");
			for (int i = 0; i < 60 * 3; i++) {
				swiWaitForVBlank(); // Wait 3 seconds
			}
			return;
		}
	} else {
		FILE *file = fopen(useTwlmPath ? "sd:/_nds/TWiLightMenu/extras/widescreen.pck" : "sd:/_nds/ntr-forwarder/widescreen.pck", "rb");
		if (file) {
			char buf[5] = {0};
			fread(buf, 1, 4, file);
			if (strcmp(buf, ".PCK") != 0) // Invalid file
				return;

			u32 fileCount;
			fread(&fileCount, 1, sizeof(fileCount), file);

			u32 offset = 0, size = 0;

			// Try binary search for the game
			int left = 0;
			int right = fileCount;

			while (left <= right) {
				int mid = left + ((right - left) / 2);
				fseek(file, 16 + mid * 16, SEEK_SET);
				fread(buf, 1, 4, file);
				int cmp = strcmp(buf, tid);
				if (cmp == 0) { // TID matches, check CRC
					u16 crc;
					fread(&crc, 1, sizeof(crc), file);

					if (crc == crc16) { // CRC matches
						fread(&offset, 1, sizeof(offset), file);
						fread(&size, 1, sizeof(size), file);
						wideCheatFound = true;
						break;
					} else if (crc < crc16) {
						left = mid + 1;
					} else {
						right = mid - 1;
					}
				} else if (cmp < 0) {
					left = mid + 1;
				} else {
					right = mid - 1;
				}
			}

			if (offset > 0) {
				fseek(file, offset, SEEK_SET);
				u8 *buffer = new u8[size];
				fread(buffer, 1, size, file);

				FILE *out = fopen("sd:/_nds/nds-bootstrap/wideCheatData.bin", "wb");
				if(out) {
					fwrite(buffer, 1, size, out);
					fclose(out);
				}
				delete[] buffer;
			}

			fclose(file);
		}
	}
	if ((wideCheatFound || force) && (access("sd:/_nds/TWiLightMenu/TwlBg/Widescreen.cxi", F_OK) == 0 || access("sd:/_nds/ntr-forwarder/Widescreen.cxi", F_OK) == 0)) {
		if (access("sd:/luma/sysmodules/TwlBg.cxi", F_OK) == 0) {
			rename("sd:/luma/sysmodules/TwlBg.cxi", (useTwlmPath ? "sd:/_nds/TWiLightMenu/TwlBg/TwlBg.cxi.bak" : "sd:/_nds/ntr-forwarder/TwlBg.cxi.bak"));
		}
		if (rename("sd:/_nds/TWiLightMenu/TwlBg/Widescreen.cxi", "sd:/luma/sysmodules/TwlBg.cxi") == 0
		 || rename("sd:/_nds/ntr-forwarder/Widescreen.cxi", "sd:/luma/sysmodules/TwlBg.cxi") == 0) {
			CIniFile ntrforwarderini("sd:/_nds/ntr_forwarder.ini");
			ntrforwarderini.SetInt("NTR-FORWARDER", "WIDESCREEN_LOADED", true);
			ntrforwarderini.SaveIniFile("sd:/_nds/ntr_forwarder.ini");

			setAutoload(resetTid);
			DC_FlushAll();
			fifoSendValue32(FIFO_USER_01, 1);
			stop();
		}
	}
}

void UnsetWidescreen() {
	bool useTwlmPath = (access("sd:/_nds/TWiLightMenu/extras/widescreen.pck", F_OK) == 0);

	CIniFile ntrforwarderini("sd:/_nds/ntr_forwarder.ini");
	ntrforwarderini.SetInt("NTR-FORWARDER", "WIDESCREEN_LOADED", false);
	ntrforwarderini.SaveIniFile("sd:/_nds/ntr_forwarder.ini");

	// Revert back to 4:3 for next boot
	if(useTwlmPath)
		mkdir("sd:/_nds/TWiLightMenu/TwlBg", 0777);
	if (rename("sd:/luma/sysmodules/TwlBg.cxi", (useTwlmPath ? "sd:/_nds/TWiLightMenu/TwlBg/Widescreen.cxi" : "sd:/_nds/ntr-forwarder/Widescreen.cxi")) != 0) {
		consoleDemoInit();
		iprintf("Failed to rename TwlBg.cxi\n");
		iprintf("back to Widescreen.cxi\n");
		for (int i = 0; i < 60*3; i++) swiWaitForVBlank();
	}
	if (access(useTwlmPath ? "sd:/_nds/TWiLightMenu/TwlBg/TwlBg.cxi.bak" : "sd:/_nds/ntr-forwarder/TwlBg.cxi.bak", F_OK) == 0) {
		rename(useTwlmPath ? "sd:/_nds/TWiLightMenu/TwlBg/TwlBg.cxi.bak" : "sd:/_nds/ntr-forwarder/TwlBg.cxi.bak", "sd:/luma/sysmodules/TwlBg.cxi");
	}
}

std::string ReplaceAll(std::string str, const std::string& from, const std::string& to) {
    size_t start_pos = 0;
    while((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); // Handles case where 'to' is a substring of 'from'
    }
    return str;
}


std::string ndsPath;
std::string romfolder;
std::string filename;
std::string savename;
std::string romFolderNoSlash;
std::string savepath;

static inline void takeWhileMsg() {
	#ifdef DSI
	iprintf ("If this takes a while, close\n");
	iprintf ("and open the console's lid.\n");
	#else
	iprintf ("If this takes a while,\n");
	iprintf ("press HOME, and press B.\n");
	#endif
	iprintf ("\n");
}

//---------------------------------------------------------------------------------
int main(int argc, char **argv) {
//---------------------------------------------------------------------------------

	defaultExceptionHandler();

	// Cut slot1 power to save battery
	disableSlot1();

	keysSetRepeat(25, 5);

	*(vu32*)0x0DFFFE0C = 0x4652544E;
	bool debugRam = (*(vu32*)0x0DFFFE0C == 0x4652544E);

	if (debugRam) {
		consoleModel = fifoGetValue32(FIFO_USER_05) == 0xD2 ? 1 : 2; // 1: Panda DSi, 2: 3DS/2DS
	}

	if (fatInitDefault()) {
		if (argc < 2 || access(argv[1], F_OK) != 0) {
			consoleDemoInit();
			if (argc < 2) {
				iprintf("sdcard.nds should not be loaded\ndirectly.\n");
				iprintf("Load a forwarder instead.\n");
			} else {
				iprintf("Not found:\n%s\n\n", argv[1]);
				iprintf("Please recreate the forwarder\n");
				iprintf("with the correct ROM path.\n");
			}
		} else {
		nitroFSInit(argv[0]);

		CIniFile ntrforwarderini( "sd:/_nds/ntr_forwarder.ini" );

		bootstrapFile = ntrforwarderini.GetInt("NTR-FORWARDER", "BOOTSTRAP_FILE", 0);
		widescreenLoaded = ntrforwarderini.GetInt("NTR-FORWARDER", "WIDESCREEN_LOADED", false);

		ndsPath = (std::string)argv[1];
		/*consoleDemoInit();
		printf(argv[1]);
		printf("\n");
		printf("Press START\n");
		while (1) {
			scanKeys();
			if (keysDown() & KEY_START) break;
			swiWaitForVBlank();
		}*/

		romfolder = ndsPath;
		while (!romfolder.empty() && romfolder[romfolder.size()-1] != '/') {
			romfolder.resize(romfolder.size()-1);
		}
		chdir(romfolder.c_str());

		filename = ndsPath;
		const size_t last_slash_idx = filename.find_last_of("/");
		if (std::string::npos != last_slash_idx)
		{
			filename.erase(0, last_slash_idx + 1);
		}

		swiWaitForVBlank();
		GameSettings gameSettings(filename, ndsPath);
		scanKeys();
		if(keysHeld() & KEY_Y) {
			gameSettings.menu();
		}

		FILE *f_nds_file = fopen(filename.c_str(), "rb");
		bool dsiBinariesFound = checkDsiBinaries(f_nds_file);
		int isHomebrew = checkIfHomebrew(f_nds_file);
		bool isDSiWare = false;

		if (isHomebrew == 0) {
			mkdir ("saves", 0777);
		}

		fclose(f_nds_file);

		extern sNDSHeaderExt ndsHeader;

		if ((ndsHeader.gameCode[0] == 0x48 && ndsHeader.makercode[0] != 0 && ndsHeader.makercode[1] != 0)
		 || (ndsHeader.gameCode[0] == 0x4B && ndsHeader.makercode[0] != 0 && ndsHeader.makercode[1] != 0)
		 || (ndsHeader.gameCode[0] == 0x5A && ndsHeader.makercode[0] != 0 && ndsHeader.makercode[1] != 0)
		 || (ndsHeader.gameCode[0] == 0x42 && ndsHeader.gameCode[1] == 0x38 && ndsHeader.gameCode[2] == 0x38))
		{ if (ndsHeader.unitCode != 0)
			isDSiWare = true; // Is a DSiWare game
		}

		if (isHomebrew == 2) {
			const char *argarray[] = {argv[1]};
			int err = runNdsFile(argarray[0], sizeof(argarray) / sizeof(argarray[0]), argarray);
			if (!consoleInited) {
				consoleDemoInit();
				consoleInited = true;
			}
			consoleDemoInit();
			iprintf("Start failed. Error %i\n", err);
			if (err == 1) iprintf ("ROM not found.\n");
		} else {
			mkdir ("sd:/_nds/nds-bootstrap", 0777);

			if (argc >= 3) {
				u32 autoloadParams[2] = {0};
				memcpy(autoloadParams, argv[2], 4);
				autoloadParams[1] = 0x00030000 | argv[2][4];

				FILE *srBackendFile = fopen("sd:/_nds/nds-bootstrap/srBackendId.bin", "wb");
				fwrite(autoloadParams, sizeof(u32), 2, srBackendFile);
				fclose(srBackendFile);
			} else {
				FILE *headerFile = fopen("sd:/_nds/ntr-forwarder/header.bin", "rb");
				if (headerFile) {
					FILE *srBackendFile = fopen("sd:/_nds/nds-bootstrap/srBackendId.bin", "wb");
					fread(__DSiHeader, 1, 0x1000, headerFile);
					fwrite((char*)__DSiHeader+0x230, sizeof(u32), 2, srBackendFile);
					fclose(srBackendFile);
					fclose(headerFile);
				} else if (access("sd:/_nds/nds-bootstrap/srBackendId.bin", F_OK) == 0) {
					remove("sd:/_nds/nds-bootstrap/srBackendId.bin");
				}
			}

			const char *typeToReplace = ".nds";
			if (extention(filename, ".dsi")) {
				typeToReplace = ".dsi";
			} else if (extention(filename, ".ids")) {
				typeToReplace = ".ids";
			} else if (extention(filename, ".srl")) {
				typeToReplace = ".srl";
			} else if (extention(filename, ".app")) {
				typeToReplace = ".app";
			}

			char savExtension[16] = ".sav";
			if(gameSettings.saveNo > 0)
				snprintf(savExtension, sizeof(savExtension), ".sav%d", gameSettings.saveNo);
			savename = ReplaceAll(filename, typeToReplace, savExtension);
            std::string saveNameFc = ReplaceAll(filename, typeToReplace, ".sav");
			romFolderNoSlash = romfolder;
			RemoveTrailingSlashes(romFolderNoSlash);
            mkdir(("/saves/"+savename).c_str(), 0777);
			savepath = "sd:/saves/"+savename+"/"+saveNameFc;

			std::string dsiWareSrlPath = ndsPath;
			std::string dsiWarePubPath = ReplaceAll(savepath, ".sav", ".pub");
			std::string dsiWarePrvPath = ReplaceAll(savepath, ".sav", ".prv");

			if (isDSiWare) {
				if ((getFileSize(dsiWarePubPath.c_str()) == 0) && (ndsHeader.pubSavSize > 0)) {
					consoleDemoInit();
					iprintf("Creating public save file...\n");
					iprintf ("\n");
					takeWhileMsg();

					static const int BUFFER_SIZE = 4096;
					char buffer[BUFFER_SIZE];
					toncset(buffer, 0, sizeof(buffer));
					char savHdrPath[64];
					snprintf(savHdrPath, sizeof(savHdrPath), "nitro:/DSiWareSaveHeaders/%x.savhdr",
						 (unsigned int)ndsHeader.pubSavSize);
					FILE *hdrFile = fopen(savHdrPath, "rb");
					if (hdrFile)
						fread(buffer, 1, 0x200, hdrFile);
					fclose(hdrFile);

					FILE *pFile = fopen(dsiWarePubPath.c_str(), "wb");
					if (pFile) {
						fwrite(buffer, 1, sizeof(buffer), pFile);
						fseek(pFile, ndsHeader.pubSavSize - 1, SEEK_SET);
						fputc('\0', pFile);
						fclose(pFile);
					}
					iprintf("Public save file created!\n");

					for (int i = 0; i < 30; i++) {
						swiWaitForVBlank();
					}
				}

				if ((getFileSize(dsiWarePrvPath.c_str()) == 0) && (ndsHeader.prvSavSize > 0)) {
					consoleDemoInit();
					iprintf("Creating private save file...\n");
					iprintf ("\n");
					takeWhileMsg();

					static const int BUFFER_SIZE = 4096;
					char buffer[BUFFER_SIZE];
					toncset(buffer, 0, sizeof(buffer));
					char savHdrPath[64];
					snprintf(savHdrPath, sizeof(savHdrPath), "nitro:/DSiWareSaveHeaders/%x.savhdr",
						 (unsigned int)ndsHeader.prvSavSize);
					FILE *hdrFile = fopen(savHdrPath, "rb");
					if (hdrFile)
						fread(buffer, 1, 0x200, hdrFile);
					fclose(hdrFile);

					FILE *pFile = fopen(dsiWarePrvPath.c_str(), "wb");
					if (pFile) {
						fwrite(buffer, 1, sizeof(buffer), pFile);
						fseek(pFile, ndsHeader.prvSavSize - 1, SEEK_SET);
						fputc('\0', pFile);
						fclose(pFile);
					}
					iprintf("Private save file created!\n");

					for (int i = 0; i < 30; i++) {
						swiWaitForVBlank();
					}
				}
			} else if (isHomebrew == 0 && (strncmp(ndsHeader.gameCode, "NTR", 3) != 0)) {
				u32 orgsavesize = getFileSize(savepath.c_str());
				u32 savesize = 524288; // 512KB (default size for most games)

				u32 gameTidHex = 0;
				tonccpy(&gameTidHex, &ndsHeader.gameCode, 4);

				for (int i = 0; i < (int)sizeof(ROMList)/12; i++) {
					ROMListEntry* curentry = &ROMList[i];
					if (gameTidHex == curentry->GameCode) {
						if (curentry->SaveMemType != 0xFFFFFFFF) savesize = sramlen[curentry->SaveMemType];
						break;
					}
				}

				if ((orgsavesize == 0 && savesize > 0) || (orgsavesize < savesize)) {
					consoleDemoInit();
					iprintf ((orgsavesize == 0) ? "Creating save file...\n" : "Expanding save file...\n");
					iprintf ("\n");
					takeWhileMsg();

					FILE *pFile = fopen(savepath.c_str(), orgsavesize > 0 ? "r+" : "wb");
					if (pFile) {
						fseek(pFile, savesize - 1, SEEK_SET);
						fputc('\0', pFile);
						fclose(pFile);
					}
					iprintf ("Done!\n");

					for (int i = 0; i < 30; i++) {
						swiWaitForVBlank();
					}
				}
			}

			// Set widescreen
			if(consoleModel == 2 && gameSettings.widescreen >= 1) {
				if(widescreenLoaded) {
					UnsetWidescreen();
				} else if(argc >= 3) {
					SetWidescreen(ndsPath.c_str(), isHomebrew, argv[2], gameSettings.widescreen == 2);
				} else {
					consoleDemoInit();
					iprintf("If using a 3DS-mode forwarder,\n");
					iprintf("reinstall bootstrap.cia to use\n");
					iprintf("widescreen.\n\n");
					iprintf("If using a DSi-mode forwarder\n");
					iprintf("then remake the forwarder.\n");
					goto error;
				}
			}

			// load cheats
			CheatCodelist codelist;
			u32 gameCode, crc32;

			bool cheatsEnabled = true;
			const char* cheatDataBin = "sd:/_nds/nds-bootstrap/cheatData.bin";
			mkdir("sd:/_nds", 0777);
			mkdir("sd:/_nds/nds-bootstrap", 0777);
			if(codelist.romData(ndsPath, gameCode, crc32)) {
				long cheatOffset; size_t cheatSize;
				// First try ntr-forwarder folder
				FILE* dat=fopen("sd:/_nds/ntr-forwarder/usrcheat.dat","rb");
				// If that fails, try TWiLight's file
				if(!dat)
					dat=fopen("sd:/_nds/TWiLightMenu/extras/usrcheat.dat","rb");

				if (dat) {
					if (codelist.searchCheatData(dat, gameCode, crc32, cheatOffset, cheatSize)) {
						codelist.parse(ndsPath);
						codelist.writeCheatsToFile(cheatDataBin);
						FILE* cheatData=fopen(cheatDataBin,"rb");
						if (cheatData) {
							u32 check[2];
							fread(check, 1, 8, cheatData);
							fclose(cheatData);
							if (check[1] == 0xCF000000 || getFileSize(cheatDataBin) > 0x1C00) {
								cheatsEnabled = false;
							}
						}
					} else {
						cheatsEnabled = false;
					}
					fclose(dat);
				} else {
					cheatsEnabled = false;
				}
			} else {
				cheatsEnabled = false;
			}
			if (!cheatsEnabled) {
				remove(cheatDataBin);
			}

			if (access("sd:/_nds/nds-bootstrap/esrb.bin", F_OK) == 0) {
				// Remove ESRB splash screen generated by TWiLight Menu++
				remove("sd:/_nds/nds-bootstrap/esrb.bin");
			}

			CIniFile bootstrapini( "sd:/_nds/nds-bootstrap.ini" );

			int donorSdkVer = 0;
			bool dsModeForced = false;

			if (isHomebrew == 0 && ndsHeader.unitCode == 2 && !dsiBinariesFound) {
				consoleDemoInit();
				iprintf ("The DSi binaries are missing.\n");
				iprintf ("Please obtain a clean ROM\n");
				iprintf ("to replace the current one.\n");
				iprintf ("\n");
				iprintf ("Press A to proceed to run in\n");
				iprintf ("DS mode.\n");
				while (1) {
					scanKeys();
					if (keysDown() & KEY_A) {
						dsModeForced = true;
						break;
					}
					swiWaitForVBlank();
				}
			}

			if (isHomebrew == 0) {
				donorSdkVer = SetDonorSDK(ndsPath.c_str());
			}

			char sfnSrl[62];
			char sfnPub[62];
			char sfnPrv[62];
			if (isDSiWare) {
				fatGetAliasPath("sd:/", dsiWareSrlPath.c_str(), sfnSrl);
				fatGetAliasPath("sd:/", dsiWarePubPath.c_str(), sfnPub);
				fatGetAliasPath("sd:/", dsiWarePrvPath.c_str(), sfnPrv);
			}

			// Fix weird bug where some settings would be cleared
			cacheFatTable = bootstrapini.GetInt("NDS-BOOTSTRAP", "CACHE_FAT_TABLE", cacheFatTable);

			// Write
			bootstrapini.SetString("NDS-BOOTSTRAP", "NDS_PATH", ndsPath);
			if (isDSiWare) {
				bootstrapini.SetString("NDS-BOOTSTRAP", "APP_PATH", sfnSrl);
				bootstrapini.SetString("NDS-BOOTSTRAP", "SAV_PATH", sfnPub);
				bootstrapini.SetString("NDS-BOOTSTRAP", "PRV_PATH", sfnPrv);
			} else {
				bootstrapini.SetString("NDS-BOOTSTRAP", "SAV_PATH", savepath);
			}
			if (isHomebrew == 0) {
				bootstrapini.SetString("NDS-BOOTSTRAP", "AP_FIX_PATH", isDSiWare ? "" : setApFix(filename.c_str()));
			}
			bootstrapini.SetString("NDS-BOOTSTRAP", "HOMEBREW_ARG", "");
			bootstrapini.SetInt("NDS-BOOTSTRAP", "BOOST_CPU", setClockSpeed(gameSettings.boostCpu, ndsPath.c_str()));
			bootstrapini.SetInt("NDS-BOOTSTRAP", "BOOST_VRAM", gameSettings.boostVram == -1 ? false : gameSettings.boostVram);
			bootstrapini.SetInt("NDS-BOOTSTRAP", "CARD_READ_DMA", setCardReadDMA(gameSettings.cardReadDMA, ndsPath.c_str()));
			bootstrapini.SetInt("NDS-BOOTSTRAP", "ASYNC_CARD_READ", setAsyncCardRead(gameSettings.asyncCardRead, ndsPath.c_str()));
			bootstrapini.SetInt("NDS-BOOTSTRAP", "DSI_MODE", dsModeForced ? 0 : (gameSettings.dsiMode == -1 ? true : gameSettings.dsiMode));
			bootstrapini.SetInt("NDS-BOOTSTRAP", "SWI_HALT_HOOK", gameSettings.swiHaltHook == -1 ? true : gameSettings.swiHaltHook);
			bootstrapini.SetInt("NDS-BOOTSTRAP", "EXTENDED_MEMORY", gameSettings.expandRomSpace == -1 ? false : gameSettings.expandRomSpace);
			//bootstrapini.SetInt("NDS-BOOTSTRAP", "CACHE_FAT_TABLE", cacheFatTable);
			bootstrapini.SetInt("NDS-BOOTSTRAP", "DONOR_SDK_VER", donorSdkVer);
			bootstrapini.SetInt("NDS-BOOTSTRAP", "PATCH_MPU_REGION", 0);
			bootstrapini.SetInt("NDS-BOOTSTRAP", "PATCH_MPU_SIZE", 0);
			bootstrapini.SetInt("NDS-BOOTSTRAP", "CONSOLE_MODEL", consoleModel);
			bootstrapini.SetInt("NDS-BOOTSTRAP", "LANGUAGE", gameSettings.language == -2 ? language : gameSettings.language);
			bootstrapini.SetInt("NDS-BOOTSTRAP", "REGION", gameSettings.region == -3 ? region : gameSettings.region);
			bootstrapini.SaveIniFile( "sd:/_nds/nds-bootstrap.ini" );

			if (isHomebrew == 1) {
				const char *argarray[] = {bootstrapFile ? "sd:/_nds/nds-bootstrap-hb-nightly.nds" : "sd:/_nds/nds-bootstrap-hb-release.nds"};
				int err = runNdsFile(argarray[0], sizeof(argarray) / sizeof(argarray[0]), argarray);
				if (!consoleInited) {
					consoleDemoInit();
					consoleInited = true;
				}
				consoleDemoInit();
				iprintf("Start failed. Error %i\n", err);
				if (err == 1) iprintf ("nds-bootstrap (hb) not found.\n");
			} else {
				const char *argarray[] = {bootstrapFile ? "sd:/_nds/nds-bootstrap-nightly.nds" : "sd:/_nds/nds-bootstrap-release.nds"};
				int err = runNdsFile(argarray[0], sizeof(argarray) / sizeof(argarray[0]), argarray);
				if (!consoleInited) {
					consoleDemoInit();
					consoleInited = true;
				}
				consoleDemoInit();
				iprintf("Start failed. Error %i\n", err);
				if (err == 1) iprintf ("nds-bootstrap not found.\n");
			}
		}
		}
	} else {
		// Subscreen as a console
		videoSetModeSub(MODE_0_2D);
		vramSetBankH(VRAM_H_SUB_BG);
		consoleInit(NULL, 0, BgType_Text4bpp, BgSize_T_256x256, 15, 0, false, true);	

		iprintf ("fatinitDefault failed!\n");
	}

error:
	iprintf ("\n");		
	iprintf ("Press B to return to\n");
	iprintf (consoleModel >= 2 ? "HOME Menu.\n" : "DSi Menu.\n");

	while (1) {
		scanKeys();
		if (keysDown() & KEY_B) fifoSendValue32(FIFO_USER_01, 1);	// Tell ARM7 to reboot into 3DS HOME Menu (power-off/sleep mode screen skipped)
		swiWaitForVBlank();
	}

	return 0;
}
