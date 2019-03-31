#include "Disque.h"
#include <string.h>

// pointeur vers la representation du disque dans la memoire du processus
static Sector* disk;

// variable pour gerer les erreurs
Disk_Error_t diskErrno; 

/*
 * Disk_Init
 *
 * Initialisation de la zone memoire.
 *
 * 
 */
int Disk_Init()
{
    // creation de l'image du disque dans la memoire et initialiser avec des 0
    disk = (Sector *) calloc(NUM_SECTORS, sizeof(Sector));
    if(disk == NULL) {
	diskErrno = E_MEM_OP;
	return -1;
    }
    return 0;
}

/*
 * Disk_Save
 * Sauvegarde de l image du disque de la memoire du process vers le fichier hote
 */
int Disk_Save(char* file) {
    //fichier hote
    FILE* diskFile;

    if (file == NULL) {
	diskErrno = E_INVALID_PARAM;
	return -1;
    }
    
    // oouverture avec fopen
    if ((diskFile = fopen(file, "w")) == NULL) {
	diskErrno = E_OPENING_FILE;
	return -1;
    }
    
    // ecriture avec fwrite
    if ((fwrite(disk, sizeof(Sector), NUM_SECTORS, diskFile)) != NUM_SECTORS) {
	fclose(diskFile);
	diskErrno = E_WRITING_FILE;
	return -1;
    }
    
    // fermerture
    fclose(diskFile);
    return 0;
}

/*
 * Disk_Load
 *
 *  Chargement de l'image du disque
 */
int Disk_Load(char* file) {
    FILE* diskFile;
    
    // error check
    if (file == NULL) {
	diskErrno = E_INVALID_PARAM;
	return -1;
    }
    
    // open
    if ((diskFile = fopen(file, "r")) == NULL) {
	diskErrno = E_OPENING_FILE;
	return -1;
    }
    
    // verifier que nous avons excactement le nombre de secteurs dans le fichier
    if ((fread(disk, sizeof(Sector), NUM_SECTORS, diskFile)) != NUM_SECTORS) {
	fclose(diskFile);
	diskErrno = E_READING_FILE;
	return -1;
    }
    
    // fermeture
    fclose(diskFile);
    return 0;
}

/*
 * Disk_Read
 *
 * Lecture d'un secteur
 */
int Disk_Read(int sector, char* buffer) {
    // verification des params
    if ((sector < 0) || (sector >= NUM_SECTORS) || (buffer == NULL)) {
	diskErrno = E_INVALID_PARAM;
	return -1;
    }
    
    // memcpy pour copier la memoire
    if((memcpy((void*)buffer, (void*)(disk + sector), sizeof(Sector))) == NULL) {
	diskErrno = E_MEM_OP;
	return -1;
    }
    
    return 0;
}

/*
 * Disk_Write
 *
 * Ecriture d'un secteur. Attention ecriture dans la memoire process
 */
int Disk_Write(int sector, char* buffer) 
{
    if((sector < 0) || (sector >= NUM_SECTORS) || (buffer == NULL)) {
	diskErrno = E_INVALID_PARAM;
	return -1;
    }
    
    if((memcpy((void*)(disk + sector), (void*)buffer, sizeof(Sector))) == NULL) {
	diskErrno = E_MEM_OP;
	return -1;
    }
    return 0;
}
