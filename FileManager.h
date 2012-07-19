#ifndef FILEMANAGER_H
#define	FILEMANAGER_H

#include <stdio.h>

#define	MAX_QPATH		64			// max length of a quake game pathname
#define	MAX_OSPATH		128			// max length of a filesystem pathname

// in memory

typedef struct {
	char name[MAX_QPATH];
	int filepos, filelen;
} packfile_t;

typedef struct pack_s {
	char filename[MAX_OSPATH];
	int handle;
	int numfiles;
	packfile_t *files;
} pack_t;

typedef struct searchpath_s {
	char filename[MAX_OSPATH];
	pack_t *pack; // only one of filename / pack will be used
	struct searchpath_s *next;
} searchpath_t;

class FileManager {
public:
	static searchpath_t *searchpaths;

	static int OpenFile(const char *filename, int *hndl);
	static int FOpenFile(const char *filename, FILE **file);
	static int FindFile(const char *filename, int *handle, FILE **file);
	static void WriteFile(const char *filename, void *data, int len);
	static void CloseFile(int h);
	/**
	 * Converts any illegal file characeters into legal ones.
	 * 
	 * @param data to check and fix
	 */
	static void MakeFilenameValid(char *data);
	/**
	 * Copy string without extension.
     */
	static void StripExtension(const char *in, char *out);
	/**
	 * Return a pointer to the start of the file extension.
     */
	static char *FileExtension(char *in);
	/**
	 * If the path doesn't contain an extension .EXT then add the provided
	 * default. 
	 */
	static void DefaultExtension(char *path, const char *extension);

	static void FileBase(const char *in, char *out);
private:

};

#endif	/* FILEMANAGER_H */
