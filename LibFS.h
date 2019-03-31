#ifndef __LibFS_h__
#define __LibFS_h__

/*
 * En tete pour declarer toutes les fonctons de notre FS
 * Ne pas modifier le contenu de ce fichier pour le projet
 * */
    
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// gestion des erreurs
extern int osErrno;
    
// error types - 
typedef enum {
    E_GENERAL,      // general
    E_CREATE, 
    E_NO_SUCH_FILE, 
    E_TOO_MANY_OPEN_FILES, 
    E_BAD_FD, 
    E_NO_SPACE, 
    E_FILE_TOO_BIG, 
    E_SEEK_OUT_OF_BOUNDS, 
    E_FILE_IN_USE, 
    E_BUFFER_TOO_SMALL, 
    E_DIR_NOT_EMPTY,
    E_ROOT_DIR,
} FS_Error_t;
    
int FS_Boot(char *path);
int FS_Sync();

// file ops
int File_Create(char *file);
int File_Open(char *file);
int File_Read(int fd, void *buffer, int size);
int File_Write(int fd, void *buffer, int size);
int File_Seek(int fd, int offset);
int File_Close(int fd);
int File_Unlink(char *file);

// directory ops
int Dir_Create(char *path);
int Dir_Size(char *path);
int Dir_Read(char *path, void *buffer, int size);
int Dir_Unlink(char *path);

#endif 
/* __LibFS_h__ */
// Credits Andrea C. Arpaci-Dusseau
// Modified by A. Gouaich