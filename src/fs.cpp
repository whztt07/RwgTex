////////////////////////////////////////////////////////////////
//
// Blood Pill - file system
// coded by Pavel [VorteX] Timofeyev and placed to public domain
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
//
// See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
////////////////////////////////

#include "main.h"
#include "zip.h"
#include "unzip.h"
#include "crc32.h"

vector<FS_File> textures;
int texturesSkipped;

void FS_SetFile(FS_File *file, char *fullpath)
{
	char c[MAX_FPATH];

	memset(file, 0, sizeof(FS_File));
	if (!fullpath[0])
		return;
	file->fullpath = fullpath;
	ExtractFilePath(fullpath, c);
	file->path = c;
	ExtractFileBase(fullpath, c);
	file->name = c;
	ExtractFileExtension(fullpath, c);
	file->ext = c;
	ExtractFileSuffix((char *)(file->name.c_str()), c, '_');
	file->suf = c;
}

void FS_SetFile(FS_File *file, char *path, char *name)
{
	char c[MAX_FPATH];

	memset(file, 0, sizeof(FS_File));
	if (!path[0])
		path = "./";
	if (!name[0])
		name = "?";
	file->path = path;
	ExtractFileBase(name, c);
	file->name = c;
	ExtractFileExtension(name, c);
	file->ext = c;
	ExtractFileSuffix((char *)(file->name.c_str()), c, '_');
	file->suf = c;
	// make fullpath
	file->fullpath = file->path.c_str();
	file->fullpath.append(name);
}

FS_File *FS_NewFile(char *filepath)
{
	FS_File *file;

	file = (FS_File *)mem_alloc(sizeof(FS_File));
	memset(file, 0, sizeof(FS_File));
	FS_SetFile(file, filepath);

	return file;
}

FS_File *FS_NewFile(char *path, char *name)
{
	FS_File *file;

	file = (FS_File *)mem_alloc(sizeof(FS_File));
	memset(file, 0, sizeof(FS_File));
	FS_SetFile(file, path, name);

	return file;
}

void FS_FreeFile(FS_File *file)
{
	mem_free(file);
}

/*
==========================================================================================

  FILE CACHE

==========================================================================================
*/

typedef struct
{
	char            filename[MAX_FPATH];
	unsigned int    crc;
	bool            used;
}
FileCacheS;
vector<FileCacheS> FileCache;

bool FS_LoadCache(char *filename)
{
	char line[2048], *crcstr;
	FileCacheS NewFC;
	FILE *f;

	FileCache.clear();
	f = fopen(filename, "r");
	if (!f)
		return false;
	while(fgets(line, 100, f) != NULL)
	{
		if (line[0] == '#' || (line[0] == '/' && line[1] == '/'))
			continue;
		// load line
		crcstr = strstr(line, " 0x");
		if (!crcstr)
			Error("FS_LoadCache: damaged line '%s'", line); 
		memset(NewFC.filename, 0, MAX_FPATH);
		strncpy(NewFC.filename, line, crcstr - line);
		NewFC.crc = ParseNum(crcstr + 1);
		NewFC.used = false;
		FileCache.push_back(NewFC);
	}
	fclose(f);
	return true;
}

void FS_SaveCache(char *filename)
{
	FILE *f;

	f = SafeOpen(filename, "w");
	fprintf(f, "# Crc32 table for source files used to generate DDS\n");
	fprintf(f, "# generated automatically, do not modify\n");
	for (std::vector<FileCacheS>::iterator file = FileCache.begin(); file < FileCache.end(); file++)
		if (file->used)
			fprintf(f, "%s 0x%08X\n", file->filename, file->crc);
	fclose(f);
}

unsigned int FS_CRC32(char *filename)
{
	DWORD crc;
	FileCrc32Assembly(filename, crc);
	return (unsigned int)crc;
}

// check if file was modified and updates cache
bool FS_CheckCache(const char *filepath, unsigned int *fileCRC)
{
	char filename[MAX_FPATH];
	unsigned int crc;

	// get crc
	sprintf(filename, "%s%s", opt_srcDir, filepath);
	if (!fileCRC)
		crc = FS_CRC32(filename);
	else
		crc = *fileCRC;

	// find in cache
	for (std::vector<FileCacheS>::iterator file = FileCache.begin(); file < FileCache.end(); file++)
	{
		if (!strnicmp(file->filename, filepath, MAX_FPATH))
		{
			file->used = true;
			if (crc == file->crc)
				return false;
			file->crc = crc;
			return true;
		}
	}

	// not found in cache, add
	FileCacheS NewFC;
	strncpy(NewFC.filename, filepath, MAX_FPATH);
	NewFC.crc = crc;
	NewFC.used = true;
	FileCache.push_back(NewFC);
	return true; 
}

/*
==========================================================================================

  TOOLS

==========================================================================================
*/

bool FS_FileMatchList(FS_File *file, vector<CompareOption> &list)
{
	// check rules
	for (vector<CompareOption>::iterator option = list.begin(); option < list.end(); option++)
	{
		// exclude rules
		if (!stricmp(option->parm.c_str(), "path!"))
		{
			if (!strnicmp(file->fullpath.c_str(), option->pattern.c_str(), strlen(option->pattern.c_str())))
				return false;
			continue;
		}
		if (!stricmp(option->parm.c_str(), "suffix!"))
		{
			if (!strnicmp(file->suf.c_str(), option->pattern.c_str(), strlen(option->pattern.c_str())))
				return false;
			continue;
		}
		if (!stricmp(option->parm.c_str(), "ext!"))
		{
			if (!strnicmp(file->ext.c_str(), option->pattern.c_str(), strlen(option->pattern.c_str())))
				return false;
			continue;
		}
		if (!stricmp(option->parm.c_str(), "name!"))
		{
			if (!strnicmp(file->name.c_str(), option->pattern.c_str(), strlen(option->pattern.c_str())))
				return false;
			continue;
		}
		// include rules
		if (!stricmp(option->parm.c_str(), "path"))
		{
			if (!strnicmp(file->fullpath.c_str(), option->pattern.c_str(), strlen(option->pattern.c_str())))
				return true;
			continue;
		}
		if (!stricmp(option->parm.c_str(), "suffix"))
		{
			if (!strnicmp(file->suf.c_str(), option->pattern.c_str(), strlen(option->pattern.c_str())))
				return true;
			continue;
		}
		if (!stricmp(option->parm.c_str(), "ext"))
		{
			if (!strnicmp(file->ext.c_str(), option->pattern.c_str(), strlen(option->pattern.c_str())))
				return true;
			continue;
		}
		if (!stricmp(option->parm.c_str(), "name"))
		{
			if (!strnicmp(file->name.c_str(), option->pattern.c_str(), strlen(option->pattern.c_str())))
				return true;
			continue;
		}
	}
	return false;
}

bool FS_FileMatchList(char *filename, vector<CompareOption> &list)
{
	FS_File file;

	FS_SetFile(&file, filename);
	return FS_FileMatchList(&file, list);
}

/*
==========================================================================================

  SCAN DIRECTORY

==========================================================================================
*/

bool FS_FindDir(char *pattern)
{
#ifdef WIN32
	WIN32_FIND_DATA n_file;
	HANDLE hFile = FindFirstFile(pattern, &n_file);
	if (hFile == INVALID_HANDLE_VALUE)
		return false;
	bool res = false;
	if (n_file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		res = true;
	FindClose(hFile);
	return res;
#else
	#error "FS_FindDir not implemented!"
#endif
}

bool FS_FindFile(char *pattern)
{
#ifdef WIN32
	WIN32_FIND_DATA n_file;
	HANDLE hFile = FindFirstFile(pattern, &n_file);
	if (hFile == INVALID_HANDLE_VALUE)
		return false;
	bool res = true;
	if (n_file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		res = false;
	FindClose(hFile);
	return true;
#else
	#error "FS_FindFile not implemented!"
#endif
}

bool AllowFile(FS_File *file)
{
	if (!FS_FileMatchList(file, opt_include))
		return false;
	return true;
}

bool AddFile(FS_File &file, bool checkinclude, unsigned int *fileCRC)
{
	if (checkinclude)
	{
		if (!AllowFile(&file))
			return false;
		if (opt_useFileCache)
		{
			if (!FS_CheckCache(file.fullpath.c_str(), fileCRC))
			{
				texturesSkipped++;
				return false;
			}
		}
	}
	// passed
	textures.push_back(file);
	return true;
}

bool AddArchive(FS_File &archive_file, bool checkinclude)
{
	char filepath[MAX_FPATH];
	FS_File file;

	if (checkinclude)
		if (!AllowFile(&archive_file))
			return false;
	sprintf(filepath, "%s%s", opt_srcDir, archive_file.fullpath.c_str());

	// open zip
	HZIP zh = OpenZip(filepath, "");
	if (!zh)
	{
		Warning("AddArchive: failed to open ZIP archive '%s'", filepath);
		return false;
	}

	// scan zip archive
	for (int i = 0; ; i++)
	{
		ZIPENTRY ze;
		ZRESULT zr = GetZipItem(zh, i, &ze);
		if (zr != ZR_OK)
		{
			if (zr != ZR_ARGS)
				Warning("AddArchive(%s): failed to open entry %i- error code 0x%08X", filepath, i, zr);
			break;
		}
		if (ze.attr & FILE_ATTRIBUTE_DIRECTORY)
			continue;
		FS_SetFile(&file, ze.name);
		            file.zipfile = filepath;
		            file.zipindex = i;
		AddFile(file, true, &ze.crc32);
	}
	CloseZip(zh);
	return true;
}

void FS_ScanPath(char *basepath, char *singlefile, char *addpath)
{
	char pattern[MAX_FPATH], path[MAX_FPATH], scanpath[MAX_FPATH];
	FS_File file;

	// start path
	strlcpy(pattern, basepath, sizeof(pattern));
	strlcpy(path, "", sizeof(path));
	if (addpath)
	{
		strlcpy(path, addpath, sizeof(path));
		strlcat(path, "/", sizeof(path));
	}
	strlcat(pattern, path, sizeof(pattern));
	if (singlefile && singlefile[0])
		strlcat(pattern, singlefile, sizeof(pattern));
	else
	{
		strlcat(pattern, "*", sizeof(pattern));
		singlefile = NULL;
	}

	// begin search
#ifdef WIN32
	WIN32_FIND_DATA n_file;
	HANDLE hFile = FindFirstFile(pattern, &n_file);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		Warning("ScanFiles: failed to open %s", pattern);
		return;
	}
	do
	{
		SimplePacifier();

		// get file
		if (!strnicmp(n_file.cFileName, ".", 1))
			continue;
		if (n_file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			strlcpy(scanpath, path, sizeof(scanpath));
			strlcat(scanpath, n_file.cFileName, sizeof(scanpath));
			FS_ScanPath(basepath, NULL, scanpath);
			continue;
		}
		FS_SetFile(&file, path, n_file.cFileName);

		// add
		if (FS_FileMatchList(&file, opt_archiveFiles))
		{
			AddArchive(file, singlefile ? false : true);
			continue;
		}
		AddFile(file, singlefile ? false : true, NULL);
	}
	while(FindNextFile(hFile, &n_file) != 0);
	FindClose(hFile);
#else

#error "FS_ScanPath not implemented!"

#endif
}	

/*
==========================================================================================

  FILE ACCESSING

==========================================================================================
*/

byte *FS_LoadFile(FS_File *file, size_t *filesize)
{
	char filename[MAX_FPATH], filepath[MAX_FPATH];
	byte *filedata;

	sprintf(filename, "%s%s.%s", file->path.c_str(), file->name.c_str(), file->ext.c_str());
	sprintf(filepath, "%s%s", opt_srcDir, filename);

	// unpack ZIP
	if (!file->zipfile.empty())
	{
		HZIP zh = OpenZip(file->zipfile.c_str(), "");
		if (!zh)
		{
			Warning("FS_LoadFile(%s:%s): failed to open archive", file->zipfile.c_str(), file->fullpath.c_str());
			return NULL;
		}
		ZIPENTRY ze;
		ZRESULT zr = GetZipItem(zh, file->zipindex, &ze);
		if (zr != ZR_OK)
		{
			Warning("FS_LoadFile(%s:%s): failed to seek ZIP entry - error code 0x%08X", file->zipfile.c_str(), file->fullpath.c_str(), zr);
			return NULL;
		}
		filedata = (byte *)mem_alloc(ze.unc_size);
		zr = UnzipItem(zh, file->zipindex, filedata, ze.unc_size);
		if (zr != ZR_OK)
		{
			mem_free(filedata);
			Warning("FS_LoadFile(%s:%s): failed to unpack ZIP entry - error code 0x%08X", file->zipfile.c_str(), file->fullpath.c_str(), zr);
			return NULL;
		}
		CloseZip(zh);
		*filesize = ze.unc_size;
		return filedata;
	}
		
	// read real file
	*filesize = LoadFileUnsafe(filepath, &filedata);
	if (*filesize < 0)
	{
		Warning("%s : cannot open file (%s)", filename, strerror(errno));
		return NULL;
	}
	return filedata;
}

/*
==========================================================================================

  COMMON

==========================================================================================
*/

void FS_Init(void)
{
}

void FS_Shutdown(void)
{
}

void FS_PrintModules(void)
{
}