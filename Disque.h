//
// Disque.h
//
// Emulation d'un disque pour lire/ecrire des secteurs
//
//

#ifndef __Disk_H__
#define __Disk_H__

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// parametres fixe
#define SECTOR_SIZE  512
#define NUM_SECTORS  10000 

// Les erreurs du disque
typedef enum {
  E_MEM_OP,
  E_INVALID_PARAM,
  E_OPENING_FILE,
  E_WRITING_FILE,
  E_READING_FILE,
} Disk_Error_t;

//structure pour un secteur du disque
typedef struct sector {
  char data[SECTOR_SIZE];
} Sector;

extern Disk_Error_t diskErrno; // variable globale pour gerer les erreurs de disque

//initialisation
int Disk_Init();
//sauvegarde des donnees sur le fichier hote
int Disk_Save(char* file);
//chargement de l image disque
int Disk_Load(char* file);
//ecriture d un secteur; attention, il faut faire un save pour persister les donnees
int Disk_Write(int sector, char* buffer);
int Disk_Read(int sector, char* buffer);
//lecture d'un secteur a partir d'un secteur

#endif // __Disk_H__
// Credits Andrea C. Arpaci-Dusseau
// Modified by A. Gouaich