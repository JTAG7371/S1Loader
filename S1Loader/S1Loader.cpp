#include <stdio.h>
#include <xtl.h>
#include <string>
#include <vector>
#include <cstdint>
#include <cwctype>
#include <algorithm>

#include "GameLib.h"

struct GameOffsets
{
	std::wstring module_name;
	std::uint32_t date_timestamp;
	std::uint32_t cwd;
	std::uint32_t scr_begin_load_scripts;
	std::uint32_t scr_read_file_fastfile;
	std::uint32_t fs_fopen_file_read_for_thread;
	std::uint32_t fs_file_read;
	std::uint32_t fs_file_close;
	std::uint32_t scr_add_source_buffer_internal;
};

GameOffsets game_offsets[8] = 
{
	// Build 1054582 Thu Sep 04 00:27:16 2014
	{ L"1-s1_sp.exe", 0x540814AA, 0x82C39EB0, 0x826E7008, 0x826EC1F8, 0x827690D8, 0x82689270, 0x82689250, 0x826EC010 },
	{ L"1-s1_mp.exe", 0x54081474, 0x82EB2688, 0x828516B8, 0x828562E8, 0x829790A8, 0x827D91D0, 0x827D91B0, 0x82856100 },
	{ L"3-s1_sp_fast_server.exe", 0x5407DF5C, 0x82B59EB0, 0x82662690, 0x82665CC0, 0x826CCB70, 0x82613AB0, 0x826CC368, 0x82665B40 },
	{ L"6-s1_mp_fast_server.exe", 0x5407DF46, 0x82FD2688, 0x82888200, 0x8288B490, 0x829864F8, 0x82817958, 0x82817938, 0x8288B310 },

	// Build 1079758 Tue Sep 16 19:48:57 2014
	{ L"1-s1_sp.exe", 0x5418F6BF, 0x82C82A50, 0x82717220, 0x8271C428, 0x8279A3E8, 0x826B9630, 0x826B9610, 0x8271C240 },
	{ L"1-s1_mp.exe", 0x5418F6DB, 0x82EFB2A8, 0x82894F50, 0x82899B98, 0x829BD950, 0x8281B8B0, 0x8281B890, 0x828999B0 },
	{ L"3-s1_sp_fast_server.exe", 0x5418F6D0, 0x82BA2A50, 0x82692600, 0x82695D70, 0x826FD968, 0x826439F8, 0x8273C408, 0x82695BF0 },
	{ L"6-s1_mp_fast_server.exe", 0x5418F6ED, 0x8301B2A8, 0x828BBE28, 0x828BF0E8, 0x829BB000, 0x8284A1F0, 0x8284A1D0, 0x828BEF68 },
};

GameOffsets* LoadForSupportedGame(std::uint32_t title_id, PLDR_DATA_TABLE_ENTRY data_table_entry)
{
	auto date_timestamp = data_table_entry->TimeDateStamp;

	// Make sure this is being loaded on the correct title
	if (title_id != 0x41560914)
	{
		return nullptr;
	}
	
	// Attempt to find correct game executable
	for (auto i = 0; i < ARRAYSIZE(game_offsets); i++)
	{
		// Compare against module name and timestamp
		if (!wcsicmp(data_table_entry->BaseDllName.Buffer, game_offsets[i].module_name.data()) && data_table_entry->TimeDateStamp == game_offsets[i].date_timestamp)
		{
			return &game_offsets[i];
		}

		// Some people may have patched executables but the timestamp should not change
		if (data_table_entry->TimeDateStamp == game_offsets[i].date_timestamp)
		{
			return &game_offsets[i];
		}
	}

	// This is a new executable potentially
	printf("Failed to find compatible executable - Title ID: 0x%X - BaseDllName: %ws - Timestamp: %X\n", title_id, data_table_entry->BaseDllName.Buffer, data_table_entry->TimeDateStamp);
	return nullptr;
}

Detour* Scr_BeginLoadScripts_t = nullptr;
Detour* Scr_ReadFile_FastFile_t = nullptr;

int(__cdecl * FS_FOpenFileReadForThread)(const char* path, int* h) = nullptr;
int(__cdecl * FS_FileRead)(void* buf, int len, int h) = nullptr;
void(__cdecl * FS_FileClose)(int h) = nullptr;

void(__cdecl * Scr_AddSourceBufferInternal)(const char *extFilename, const char *codePos, char *sourceBuf, int len, bool doEolFixup, bool archive, bool newBuffer) = nullptr;

std::vector<char*> cached_scripts;

void Scr_BeginLoadScripts()
{
	printf("Resetting Script Cache\n");

	for (auto i = 0u; i < cached_scripts.size(); i++)
	{
		free(cached_scripts[i]);
	}

	cached_scripts.clear();

	Scr_BeginLoadScripts_t->CallOriginal();
}

char * Scr_ReadFile_FastFile(const char *filename, const char *extFilename, const char *codePos, bool archive)
{
	char buffer[512] = { 0 };
	sprintf(buffer, "raw/%s", filename);

	auto file_handle = 0;
	auto file_size = FS_FOpenFileReadForThread(buffer, &file_handle);

	if (file_size == -1)
	{
		return reinterpret_cast<char*>(Scr_ReadFile_FastFile_t->CallOriginal(filename, extFilename, codePos, archive));
	}

	char* script_buffer = reinterpret_cast<char*>(calloc(file_size + 1, 1));
	FS_FileRead(script_buffer, file_size, file_handle);
	FS_FileClose(file_handle);

	Scr_AddSourceBufferInternal(extFilename, codePos, script_buffer, file_size, true, archive, true);

	printf("Loaded Script File: %s\n", filename);

	cached_scripts.push_back(script_buffer);
	return script_buffer;
}

BOOL __stdcall DllMain(HANDLE hHandle, DWORD dwReason, LPVOID lpReserved)
{
	if (dwReason == DLL_PROCESS_ATTACH) 
	{
		printf("S1Loader Loaded!\n");

		if (auto* game = LoadForSupportedGame(XamGetCurrentTitleId(), *XexExecutableModuleHandle))
		{
			// Initialize Functions
			FS_FOpenFileReadForThread = (decltype(FS_FOpenFileReadForThread))game->fs_fopen_file_read_for_thread;
			FS_FileRead = (decltype(FS_FileRead))game->fs_file_read;
			FS_FileClose = (decltype(FS_FileClose))game->fs_file_close;
			Scr_AddSourceBufferInternal = (decltype(Scr_AddSourceBufferInternal))game->scr_add_source_buffer_internal;

			// Replace D: with game: to allow reading using game functions
			strcpy(reinterpret_cast<char*>(game->cwd), "game:");

			Scr_BeginLoadScripts_t = new Detour(game->scr_begin_load_scripts, reinterpret_cast<DWORD>(Scr_BeginLoadScripts));
			Scr_ReadFile_FastFile_t = new Detour(game->scr_read_file_fastfile, reinterpret_cast<DWORD>(Scr_ReadFile_FastFile));

			return TRUE;
		}

		return FALSE;
	}
	else if (dwReason == DLL_PROCESS_DETACH)
	{
		printf("S1Loader Unloaded!\n");
		
		if (Scr_BeginLoadScripts_t)
		{
			delete Scr_BeginLoadScripts_t;
		}

		if (Scr_ReadFile_FastFile_t)
		{
			delete Scr_ReadFile_FastFile_t;
		}
	}

	return TRUE;
}