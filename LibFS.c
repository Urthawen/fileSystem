#include "LibFS.h"
#include "Disque.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define MAGIC_NUMBER 1999
#define MAGIC_NUMBER_BYTES 5

#define DIR_ENTRY_SIZE 20

#define DIR_ENTRY_PER_BLOCK (SECTOR_SIZE / sizeof(dir_entry))
#define INODE_PER_BLOCK 4

#define INODE_SIZE 128
#define NUMBER_OF_DATA_BLOCK 30

#define MAX_FILE_NAME 16
#define MAX_PATH_LENGTH 256

#define NB_SECTOR 10000
#define NB_INODE 8192

#define INODE_OFFSET 5
#define DATA_OFFSET 2048+INODE_OFFSET

#define DIRECTORY 1
#define FILE 0

#define ROOT_INODE 0

/////////////////
//// BITMAP ////
////////////////

//Merci Louis Parent

static char Imap[1024];
static char Dmap[1024];

int  loadmaps()// pour lire les bitmaps et les mettre dans les variables statiques
{
  if ( (Disk_Read(1, Imap)  == -1) || (Disk_Read(2, Imap+512)  == -1) ) {
    printf("Disk_Read() Imap failed\n");
    osErrno = E_GENERAL;
    return -1;
  }
  if ( (Disk_Read(3, Dmap)  == -1) || (Disk_Read(4, Dmap+512)  == -1) ) {
    printf("Disk_Read() Dmap failed\n");
    osErrno = E_GENERAL;
    return -1;
  }
  return 0;
}

int  savemaps()// pour sauvegarder les variables statiques sur le disque virtuel
{
  if ( (Disk_Write(1, Imap)  == -1) || (Disk_Write(2, Imap+512)  == -1) ) {
    printf("Disk_Read() Imap failed\n");
    osErrno = E_GENERAL;
    return -1;
  }
  if ( (Disk_Write(3, Dmap)  == -1) || (Disk_Write(4, Dmap+512)  == -1) ) {
    printf("Disk_Read() Dmap failed\n");
    osErrno = E_GENERAL;
    return -1;
  }
  return 0;
}

int readbit(char c, int pos)
{ // return 1, s'il y a un 1 sur la position pos dans c
  char conv[]={0x80, 0x40,  0x20, 0x10,  0x08, 0x04, 0x02, 0x01};
  if (pos < 0 || pos > 7) {
    printf("bit: incorrect position \n");
    osErrno = E_GENERAL;
    return -1;
  }
  c = c & conv[pos];
  if (c == '\0')
    return 0;
  else
    return 1;
}

int setbit(char* c, int pos, int val)
{ // marquer val sur la position pos dans c, val = 0  ou 1
  char conv[]={0x80, 0x40,  0x20, 0x10,  0x08, 0x04, 0x02, 0x01};
  char anticonv[]={0x7F, 0xBF, 0xDF, 0xEF, 0xF7, 0xFB, 0xFD, 0xFE};
  if (pos < 0 || pos > 7) {
    printf("bit: incorrect position \n");
    osErrno = E_GENERAL;
    return -1;
  }
  if (val < 0 || val > 1) {
    printf("bit: incorrect value \n");
    osErrno = E_GENERAL;
    return -1;
  }
  if (val == 1)
    *c = (*c) | conv[pos];
  else
    *c = (*c) & anticonv[pos];
  return 0;
}

int  readpos(char * M, int pos)  // retourne le bit M[pos] valable pour les deux maps
{
  if (pos < 0 || pos > 8191) {
    printf("bit: incorrect position \n");
    osErrno = E_GENERAL;
    return -1;
  }
  int ind = pos / 8;
  int p = pos % 8;
  return readbit(M[ind],p);
}


int  setpos(char * M, int pos, int val)  // change le bit M[pos] valable pour les deux maps
{
  if (pos < 0 || pos > 8191) {
    printf("bit: incorrect position \n");
    osErrno = E_GENERAL;
    return -1;
  }
  if (val < 0 || val > 1) {
    printf("bit: incorrect value \n");
    osErrno = E_GENERAL;
    return -1;
  }
  int ind = pos / 8;
  int p = pos % 8;
  return setbit(M+ind,p,val);
}

int  findfree(char * M)  // retourne l'indice du premier bit 0 valable pour les deux maps
{
  int i;
  for(i=0; i < 8192; i++) {
    if (readpos(M,i)==0)  break;
  }
  if (i== 8192) {
    printf("bitmap: full \n");
    osErrno = E_GENERAL;
    return -1;
  }
  return i;
}

///////////////
//// INODE ////
///////////////

typedef struct t_inode
{
  int tf;  //type fichier, 0 data, 1 repertoire
  int sz;  // taille en octets
  int adr[NUMBER_OF_DATA_BLOCK];  // adresses des blocs
} inode;


inode sect_in[INODE_PER_BLOCK];  // un secteur de la table d'inodes
char * pch = (char *) sect_in;  // si besoin

inode  readinode( int I)// retourne l'inode d'indice I
{
  int ind = I / INODE_PER_BLOCK;  //indice du bloc
  int p = I % INODE_PER_BLOCK;    // indice interne
  inode sect_in[INODE_PER_BLOCK];  //bloc d'inodes

  if  (Disk_Read(INODE_OFFSET+ind, (char *) sect_in)  == -1 ) {
    printf("Disk_Read() Itable failed\n");
    osErrno = E_GENERAL;
    exit( -1);
  }
  return sect_in[p];
}

int  saveinode(inode i, int I)// modifie l'inode d'indice I
{
  int ind = I / INODE_PER_BLOCK;  //indice du bloc
  int p = I % INODE_PER_BLOCK;    // indice interne
  inode sect_in[INODE_PER_BLOCK];  //bloc d'inodes

  if  (Disk_Read(INODE_OFFSET+ind, (char *) sect_in)  == -1 ) {
    printf("Disk_Read() Itable failed\n");
    osErrno = E_GENERAL;
    return -1;
  }

  sect_in[p] = i;
  if  (Disk_Write(INODE_OFFSET+ind, (char *) sect_in)  == -1 ) {
    printf("Disk_Write() Itable failed\n");
    osErrno = E_GENERAL;
    return -1;
  }

  return 0;
}

void initInode(inode* i, int type, int size)//Initialise proprement un Inode
{
  i->tf = type;
  i->sz = size;

  for(int n = 0; n < NUMBER_OF_DATA_BLOCK; n++)
    {
      i->adr[n] = -1;//-1 par dfault pour les blocs non dfini
    }
}

/////////////
//// DIR ////
/////////////

typedef struct t_dir_entry//Entry in the directory's data
{
  char* file;//File name
  int inode;//Inode of the file
} dir_entry;

int addDirEntry(int datablock, int numEntry, char* fileName, int inode)//Ajoute une nouvelle entr dans le dossier
{
  dir_entry sect_in[DIR_ENTRY_PER_BLOCK];  //bloc d'entr de disseur

  if (Disk_Read(DATA_OFFSET+datablock, (char *) sect_in)  == -1 ) {
    printf("Disk_Read() Dtable failed\n");
    osErrno = E_GENERAL;
    return -1;
  }

  dir_entry entry;
  entry.file = fileName;
  entry.inode = inode;

  sect_in[numEntry] = entry;
  if  (Disk_Write(DATA_OFFSET+datablock, (char *) sect_in)  == -1 ) {
    printf("Disk_Write() Dtable failed\n");
    osErrno = E_GENERAL;
    return -1;
  }

  return 0;
}

int addDirEntry_last(inode* dir, char* filename, int inode)
{
  int placed = 0;
  int i = 0;

  while(i < NUMBER_OF_DATA_BLOCK && placed == 0)
    {
      if(dir->adr[i] != -1)
	{
	  char buffer[SECTOR_SIZE];

	  if(Disk_Read(DATA_OFFSET + dir->adr[i], buffer) == -1)
	    {
	      printf("Disk_Read() failed\n");
	      osErrno = E_CREATE;
	      return -1;
	    }

	  int j = 0;
	  while(j < DIR_ENTRY_PER_BLOCK && placed == 0)
	    {
	      dir_entry* entry = (dir_entry*) (buffer + (j * sizeof(dir_entry)));

	      if(entry->inode == 0)
		{
		  entry->file = filename;
		  entry->inode = inode;

		  if(Disk_Write(DATA_OFFSET + dir->adr[i], buffer) == -1)
		    {
		      printf("Disk_Write() failed\n");
		      osErrno = E_CREATE;
		      return -1;
		    }

		  dir->sz += DIR_ENTRY_SIZE;
		  placed = 1;
		}
	      j++;
	    }
	}
      i++;
    }

  return placed == 0 ? -1 : 0;
}

int delDirEntry(inode* dir, char* filename)
{
  int removed = 0;
  int i = 0;

  while(i < NUMBER_OF_DATA_BLOCK && removed == 0)
    {
      if(dir->adr[i] != -1)
	{
	  char buffer[SECTOR_SIZE];

	  if(Disk_Read(DATA_OFFSET + dir->adr[i], buffer) == -1)
	    {
	      printf("Disk_Read() failed\n");
	      osErrno = E_CREATE;
	      return -1;
	    }

	  int j = 0;
	  while(j < DIR_ENTRY_PER_BLOCK && removed == 0)
	    {
	      dir_entry* entry = (dir_entry*) (buffer + (j * sizeof(dir_entry)));

	      if(strcmp(filename, entry->file) == 0)
		{
		  entry->file = NULL;
		  entry->inode = 0;

		  if(Disk_Write(DATA_OFFSET + dir->adr[i], buffer) == -1)
		    {
		      printf("Disk_Write() failed\n");
		      osErrno = E_CREATE;
		      return -1;
		    }

		  dir->sz -= DIR_ENTRY_SIZE;
		  removed = 1;
		}
	      j++;
	    }
	}
      i++;
    }

  return removed == 0 ? -1 : 0;
}

int getInodeForName(inode dir, char* filename)
{
  int find = 0;
  int inode = -1;

  int i = 0;

  while(i < NUMBER_OF_DATA_BLOCK && find == 0)
    {
      if(dir.adr[i] != -1)
	{
	  char buffer[SECTOR_SIZE];
	  if(Disk_Read(DATA_OFFSET + dir.adr[i], buffer) == -1)
	    {
	      printf("Disk_Read() failded\n");
	      osErrno = E_CREATE;
	      return -1;
	    }

	  int j = 0;
	  while(j < DIR_ENTRY_PER_BLOCK && find == 0)
	    {
	      dir_entry* entry = (dir_entry*) (buffer + (j * sizeof(dir_entry)));
	      if(entry->file != NULL && filename != NULL)
		{
		  if(strcmp(entry->file, filename) == 0)
		    {
		      inode = entry->inode;
		      find = 1;
		    }
		}
	      j++;
	    }
	}
      i++;
    }

  return inode;
}

int findDirInode(char** folders, int folder)
{
  int in = ROOT_INODE;
  for(int i = 1; i <= folder; i++)
    {
      inode I = readinode(in);
      in = getInodeForName(I, folders[i]);
      if(in == -1)
	{
	  printf("Dir Not Found\n");
	  osErrno = E_CREATE;
	  return -1;
	}
    }

  return in;
}

//// /!\ Temporaire
int setRoot()
{
  //Rcupration de l'inode
  int index = findfree(Imap);
  if(setpos(Imap, index, 1) == -1)
    {
      printf("setpos() for inode failed\n");
      osErrno = E_GENERAL;
      return -1;
    }

  //cration de la structure
  inode root;
  initInode(&root, 1, 0);
  root.adr[0] = findfree(Dmap);

  if(setpos(Dmap, root.adr[0], 1) == -1)
    {
      printf("setpos() for data failed\n");
      osErrno = E_GENERAL;
      return -1;
    }

  //Enregistrement de l'inode
  if(saveinode(root, index) == -1)
    {
      printf("saveinode() failed\n");
      osErrno = E_GENERAL;
      return -1;
    }

  return 0;
}

////////////////
//// LIB FS ////
////////////////
int format()//Formate le disque dur
{
  //Initialisation du super block
  char superblock[SECTOR_SIZE];
  for(int i = 0; i < SECTOR_SIZE; i++)
    {
      superblock[i] = 0;
    }

  //Ecriture Magic Number
  sprintf(&superblock[SECTOR_SIZE - MAGIC_NUMBER_BYTES], "0%i", MAGIC_NUMBER);
  Disk_Write(0, superblock);

  //Load Bitmaps
  if(loadmaps() == -1)
    {
      printf("loadmaps() failed\n");
      osErrno = E_GENERAL;
      return -1;
    }

  //Create root directory
  if(setRoot() == -1)
    {
      printf("setRoot() failed\n");
      osErrno = E_GENERAL;
      return -1;
    }

  return 0;
}

int pathSize(char* path)
{
  int size = 0;
  int i = 0;

  while(path[i] != '\0')
    {
      if(path[i] == '/')
	{
	  size++;
	}
      i++;
    }

  return path[1] != '\0' ? size + 1 : size;
}

int pathToArray(char* path, char** array)
{
  int size = 1;
  char* current = strtok(path, "/");

  if(current != NULL)
    {
      array[0] = NULL;
      do
	{
	  array[size] = current;
	  size++;
	}while((current = strtok(NULL, "/")) != NULL);
      return size;
    }
  else if(size == 1)
    {
      array[0] = NULL;
      return size;
    }
  else
    {
      printf("No Absolute Path");
      return -1;
    }
}

int osErrno;
char* imageFile;

int FS_Boot(char *path)
{
  printf("My FS\n");
  printf("FS_Boot %s\n", path);

  //Init
  if (Disk_Init() == -1)
    {
      printf("Disk_Init() failed\n");
      osErrno = E_GENERAL;
      return -1;
    }

  //Load
  if(Disk_Load(path) == -1)
    {
      if(diskErrno == E_OPENING_FILE)
	{
	  if(format() == -1)
	    {
	      printf("Format() failed\n");
	      osErrno = E_GENERAL;
	      return -1;
	    }

	  if(savemaps() == -1)//Save Bitmaps
	    {
	      printf("savemaps() failed\n");
	      osErrno = E_GENERAL;
	      return -1;
	    }
	}
      else
	{
	  printf("Disk_Load() failed\n");
	  osErrno = E_GENERAL;
	  return -1;
	}
    }

  char buffer[sizeof(Sector)];
  //Lecture superblock
  if(Disk_Read(0, buffer) == -1)
    {
      printf("Disk_Read() failed\n");
      osErrno = E_GENERAL;
      return -1;
    }

  //Comparaison Magic Number
  int magicNumber = atoi(&buffer[SECTOR_SIZE - MAGIC_NUMBER_BYTES]);
  if(magicNumber != MAGIC_NUMBER)
    {
      printf("Unknow Magic Number\n");
      osErrno = E_GENERAL;
      return -1;
    }

  //Charge les bitmaps
  if(loadmaps() == -1)
    {
      printf("loadmaps() failed\n");
      osErrno = E_GENERAL;
      return -1;
    }

  imageFile = path;
  FS_Sync();
  return 0;
}

int FS_Sync()
{
  printf("FS_Sync\n");
  if(savemaps() == -1)//Save Bitmaps
    {
      printf("savemaps() failed\n");
      osErrno = E_GENERAL;
      return -1;
    }
  if(Disk_Save(imageFile) == -1)//Save Disk
    {
      printf("Disk_Save() failed\n");
      osErrno = E_GENERAL;
      return -1;
    }
  return 0;
}

int File_Create(char *file)
{
  printf("FS_Create\n");
  // TODO...
  return 0;
}

int File_Open(char *file)
{
  printf("FS_Open\n");
  // TODO...
  return 0;
}

int File_Read(int fd, void *buffer, int size)
{
  printf("FS_Read\n");
  // TODO...
  return 0;
}

int File_Write(int fd, void *buffer, int size)
{
  printf("FS_Write\n");
  // TODO...
  return 0;
}

int File_Seek(int fd, int offset)
{
  printf("FS_Seek\n");
  // TODO...
  return 0;
}

int File_Close(int fd)
{
  printf("FS_Close\n");
  // TODO...
  return 0;
}

int
File_Unlink(char *file)
{
  printf("FS_Unlink\n");
  // TODO...
  return 0;
}


// directory ops
int Dir_Create(char *path)
{
  printf("Dir_Create %s\n", path);

  int index = findfree(Imap);
  if(setpos(Imap, index, 1) == -1)
    {
      printf("setpos() for inode failed\n");
      osErrno = E_GENERAL;
      return -1;
    }

  inode dir;
  initInode(&dir, 1, 0);
  dir.adr[0] = findfree(Dmap);

  if(setpos(Dmap, dir.adr[0], 1) == -1)
    {
      printf("setpos() for data failed\n");
      osErrno = E_GENERAL;
      return -1;
    }

  if(saveinode(dir, index) == -1)
    {
      printf("saveinode() failed\n");
      osErrno = E_GENERAL;
      return -1;
    }

  char* folders[pathSize(path)];
  int length = pathToArray(path, folders);
  if(length == -1)
    {
      printf("pathToArray() failed\n");
      osErrno = E_CREATE;
      return -1;
    }

  if(!(length == 1 && strcmp(folders[0], "/" ) == 0))
    {
      int parentInode = findDirInode(folders, length-2);
      if(parentInode == -1)
	{
	  printf("finDirInode() failed\n");
	  osErrno = E_CREATE;
	  return -1;
	}
      inode parent = readinode(parentInode);
      if(addDirEntry_last(&parent, folders[length-1], index) == -1)
	{
	  printf("AddDirEntry_last() failed\n");
	  osErrno = E_CREATE;
	  return -1;
	}

      if(saveinode(parent, parentInode) == -1)
	{
	  printf("saveinode() failed\n");
	  osErrno = E_CREATE;
	  return -1;
	}
    }

  return 0;
}

int Dir_Size(char *path)
{
  printf("Dir_Size %s\n", path);

  char* folders[pathSize(path)];
  int length = pathToArray(path, folders);
  if(length == -1)
    {
      printf("pathToArray() failed\n");
      osErrno = E_CREATE;
      return -1;
    }
  int I = findDirInode(folders, length-1);

  if(I == -1)
    {
      printf("findDirInode() failed\n");
      osErrno = E_CREATE;
      return -1;
    }

  return readinode(I).sz;
}

int Dir_Read(char *path, void *buffer, int size)
{
  printf("Dir_Read %s\n", path);
  if(size < Dir_Size(path))
    {
      printf("Buffer Too Small\n");
      osErrno = E_BUFFER_TOO_SMALL;
      return -1;
    }

  int nb = 0;

  char* folders[pathSize(path)];
  int length = pathToArray(path, folders);
  if(length == -1)
    {
      printf("pathToArray() failed\n");
      osErrno = E_CREATE;
      return -1;
    }
  int I = findDirInode(folders, length-1);

  if(I == -1)
    {
      printf("findDirInode() failed\n");
      osErrno = E_CREATE;
      return -1;
    }

  inode i = readinode(I);
  for(int j = 0; j < NUMBER_OF_DATA_BLOCK; j++)
    {
      if(i.adr[j] != -1)
	{
	  char block[SECTOR_SIZE];
	  if(Disk_Read(DATA_OFFSET + i.adr[j], block) == -1)
	    {
	      printf("Disk_Read() failed\n");
	      osErrno = E_CREATE;
	      return -1;
	    }
	  char* bufferAlias = (char*) buffer;
	  for(int k = 0; k < DIR_ENTRY_PER_BLOCK; k++)
	    {
	      dir_entry* entry = (dir_entry*) (block + (k * sizeof(dir_entry)));
	      if(entry->inode != 0)
		{
		  strcpy(&bufferAlias[j * DIR_ENTRY_SIZE], entry->file);
		  bufferAlias[j * DIR_ENTRY_SIZE + MAX_FILE_NAME] = entry->inode;
		  nb++;

		}
	    }
	}
    }

  return nb;
}

int Dir_Unlink(char *path)
{
  printf("Dir_Unlink %s\n", path);

  if(strcmp(path, "/") == 0)
    {
      printf("Root Directory\n");
      osErrno = E_ROOT_DIR;
      return -1;
    }
  else if(Dir_Size(path) > 0)
    {
      printf("Dir Not Empty\n");
      osErrno = E_DIR_NOT_EMPTY;
      return -1;
    }

  char* folders[pathSize(path)];
  int length = pathToArray(path, folders);
  if(length == -1)
    {
      printf("pathToArray() failed\n");
      osErrno = E_CREATE;
      return -1;
    }
  int Dir = findDirInode(folders, length-1);

  if(Dir == -1)
    {
      printf("findDirInode() failed\n");
      osErrno = E_CREATE;
      return -1;
    }

  inode dir = readinode(Dir);

  char blank[SECTOR_SIZE];
  for(int i = 0; i < SECTOR_SIZE; i++)
    {
      blank[i] = 0;
    }

  for(int i = 0; i < NUMBER_OF_DATA_BLOCK; i++)
    {
      if(dir.adr[i] != -1)
    	{
	  if(Disk_Write(dir.adr[i], blank) == -1)
	    {
	      printf("Disk_Write() failed\n");
	      osErrno = E_GENERAL;
	      return -1;
	    }

	  if(setpos(Dmap, dir.adr[0], 0) == -1)
	    {
	      printf("setpos() Dmap failed\n");
	      osErrno = E_GENERAL;
	      return -1;
	    }
    	}
    }

  dir.tf = 0;
  dir.sz = 0;

  if(setpos(Imap, Dir, 0) == -1)
    {
      printf("setpos() Imap failed\n");
      osErrno = E_GENERAL;
      return -1;
    }//morganethebest

  int Parent = findDirInode(folders, length-2);
  inode parent = readinode(Parent);
  if(delDirEntry(&parent, folders[length-1]) == -1)
    {
      printf("delDirEntry() failed\n");
      osErrno = E_GENERAL;
      return -1;
    }

  if(saveinode(parent, Parent) == -1)
    {
      printf("saveinode() failed\n");
      osErrno = E_CREATE;
      return -1;
    }

  return 0;
}
