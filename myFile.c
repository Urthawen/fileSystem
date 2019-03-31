#include "LibFS.h"
#include "Disque.h"
#include <stdio.h>
#include <string.h>
#include <alloca.h>

// variable errno pour gerer les erreurs
int osErrno;

//***

static char* fileName ;
#define MAX_INODES 8192;
#define MAX_NAME_SIZE 16
#define MAX_OPEN_FILES 255
#define DATA_BLOCK_PER_INODE 30

typedef unsigned char byte;


typedef struct __attribute__((__packed__))  data_bloc_t {
    byte data[512];
} data_bloc_t;

typedef struct __attribute__((__packed__))  inode_bloc {
    int type;
    int size;
    int pointers[30];
} inode_bloc_t;


typedef struct __attribute__((__packed__))  inode_bitmap {
    byte map[1024];
} inode_bitmap_t;

typedef struct __attribute__((__packed__))  databloc_bitmap {
    byte map[1024];
} databloc_bitmap_t ;

typedef struct __attribute__((__packed__))  superblock {
byte unused[508] ;
int magicnumber ;
} superblock_t ;

typedef struct __attribute__((__packed__))  directory_entry {
char name[16] ;
int index ;
} directory_entry_t ;

typedef struct __attribute__((__packed__))  descriptor_entry {
int used;
int inode_index ;
int read_write_index ;
} descriptor_entry_t ;



//tableau des fichiers ouverts
static descriptor_entry_t _open_file_table [MAX_OPEN_FILES];
//indicateur pour savoir si le tableau des fichiers ouverts a ete initialise
static int _is_open_filetable_init = 0;
//magic number
#define MAGICNUMBER 0xCAFEBAFE
//types pour les repertoires et les fichiers
#define DIRECTORY_TYPE 1
#define FILE_TYPE 0

//des decalages poru le calcul des index inode
#define INODE_OFFSET 5 // le numero de secteur pour les inodes
#define DB_OFFSET (2048 + INODE_OFFSET) // le numero de secteur pour les databloc

/***
 *  Fonctions internes
 *
 */

//fonction utile pour creer la racine '/'
int _create_root_inode();

//Fonctions la gestion des maps pour les inodes et les data bloc
int _writeInodeMap(const char* map);
int _writeDBMap(const char* map);
int _loadDBMap(char* map);
int _loadInodeMap(char* map);

//Groupe de fonctions pour la traduction entre path et inode et gestion des paths

//copie d un path en retirant le dernier token, utile pour avoir le path contenant
int _copy_trim_last_token(char* destPath, const char* srcPath);
// fonction pour obtenir le path du repertoire contenant et aussi le dernier token
int _get_containing_path_and_lastname(char* containingpath, char* lastToken, const char* path);


//fonction pour obtenir l'inode partir d un path
int _path_2_inode_number(const char* path);
//fonction pour obtenir l inode et l index a partir d un path
int _path_2_inode(const char* path, inode_bloc_t* ptr, int* indexPtr);

//recherche d une entree dans le contenu d un repertoire
int _exists_key(const char* keychain, const char* key, int size);
//demande de creation d une nouvelle entree dans le contenu d un repertoire
int _create_new_directory_entry(int index, const char* newEntryName, int entryType);


//fonction de creations des inodes, elles retournent l index
int _create_new_directory_inode();
int _create_new_file_inode();

//recherche d une entree dans un repertoire
int _lookup_directory_entry(const int inodeNum, const char* entryName);


//Fonction pour ecrire le contenu d un fichier
int _write_file_content(char* buffer, int size, inode_bloc_t* inode_ptr);
int _copy_file_content(void* ptr, const inode_bloc_t* inode_ptr);

//Fonctions pour la lecture et ecriture des inodes sur le disque
int _getinodeByNumber(const int num, inode_bloc_t* ptr);
int _setinodeByNumber(const int num, inode_bloc_t* ptr);



//Groupe de fonctions pour la gestion des inodes et datablocs
// Fonction reserver et prendre un inode/bloc libre
int _find_take_free_inode();
int _find_take_free_databloc();
int _findfreeFromMap(char * map)  ;
// fonction utiles pour la lecture de bits sur  les maps
int _setpos(char * map, int pos, int val) ;
int _readpos(char * map, int pos) ;
int _setbit(char* c, int pos, int val);
int _readbit(char c, int pos) ;




//Groupe de fonctions pour la gestion des table
int _init_open_file_table();


/**
*
* SECTION DES FONCTIONS UTILITAIRES
*
**/
//
int _init_open_file_table(){
    if(_is_open_filetable_init) return 0;
    memset( _open_file_table, 0,sizeof(descriptor_entry_t)*MAX_OPEN_FILES);
    _is_open_filetable_init = 1;
    return 0;
}


//***
int formatDisc() {
superblock_t sbloc;
inode_bitmap_t inodemap ;
databloc_bitmap_t dbmap;
memset(&sbloc, 0, sizeof(superblock_t));
sbloc.magicnumber = MAGICNUMBER ;
memset(&inodemap, 0, sizeof(inode_bitmap_t));
//create a root inode
inodemap.map[0] = 0x01;
//
memset(&dbmap, 0, sizeof(inode_bitmap_t));
//
Disk_Write(0,(char*)&sbloc);
Disk_Write(1,(char*)&(inodemap.map));
Disk_Write(2,(char*)&(inodemap.map)+sizeof(Sector));
Disk_Write(3,(char*)&(dbmap.map));
Disk_Write(4,(char*)&(dbmap.map)+sizeof(Sector));
//inode table start here
// L inode 0 est reservee pour la racine
_create_root_inode();
return 0;
}


/*
 * Creation de l'inode pour la racine
 */
int _create_root_inode(){
// un secteur comme buffer de travail
Sector sector ;
memset(&sector,(byte)0,sizeof(Sector));
inode_bloc_t* inode = (inode_bloc_t*) &sector; //  inodes pour faire un secteur
//reset de la memoire
//remplir les champs pour l inode 0 celle de la racine
inode->type = DIRECTORY_TYPE ;
inode->size = 0; // le repertoire est vide
//copie du tableau d'inodes dans le secteur des inodes
// ne pas oublier de mettre toujours le decalage pour avoir le bon secteur sur le disque
if( Disk_Write(INODE_OFFSET+0, (char*) & sector) == -1 )
{
    osErrno = E_ROOT_DIR;
    return -1;
};
return 0;
}

/* Fonction qui donne un index inode a partir d un chemin*/
int _path_2_inode_number(const char* path)
{//verifications simples
    if( (strlen(path) == 0) )
    {
        osErrno =  E_GENERAL;
        return -1;
    }

    if(path[0]!='/'){
        osErrno =  E_NO_SUCH_FILE;
        return -1;
    }
    else
    {
        //le travail commence ici
        if(strcmp(path,"/") == 0) // cas ou le path designe la racine
        {
            //on retourne inode 0 pour la racine
            return 0;
        }

        //copie du path car la fonction strtok modifie son parametre
        char* actual_path_cpy = (char*) alloca(strlen(path)+1);
        strcpy(actual_path_cpy,path);//utilisation de strcpy

        actual_path_cpy = actual_path_cpy + 1;// on ignore la premier caractere qui la racine

        int current_inode = 0;
        for (char *token = strtok(actual_path_cpy,"/"); token != NULL; token = strtok(NULL, "/"))
        {
            //demande pour avoir l index de cette entree sur le repertoire courant de recherche
            current_inode = _lookup_directory_entry(current_inode,token);
            if(current_inode == -1)
            {//aie on ne trouve le token dans le repertoire courant alors le chemin est invalide
                osErrno =  E_NO_SUCH_FILE;
                return -1;
            }
        }
        //bingo j ai trouve ici inode du dernier token sur le path je le retourne
        return current_inode;
    }

}





/*
 * Fonction qui cherche une entree a partir de l index du repertoire
 */
int _lookup_directory_entry(const int inodeNum, const char* entryName)
{
//get directory inode
inode_bloc_t inode;
if( _getinodeByNumber( inodeNum , &inode ) == -1)
{
    osErrno = E_NO_SUCH_FILE;
    return -1;
}
//verification qu il s agit bien d une entree repertoire et qu il n est pas vide
if( inode.type != DIRECTORY_TYPE )
{
    osErrno = E_GENERAL;
    return -1;
}

// et qu il n est pas vide
if( inode.size == 0 )
{
osErrno = E_NO_SUCH_FILE;
return -1;
}

//lecture du contenu du repertoire pour cherche le nom
//allocation de la memoire pour recevoir localement tout le contenu
char* data = alloca(inode.size);
/*solution simple: copier tout le contenu pour travailler localement*/
if( _copy_file_content(data,&inode) == -1) //
{
    osErrno = E_GENERAL;
    return -1;
}
//nous avons maintenant une copie locale du contenu

// nous allons cherche l entree souhaitee
directory_entry_t* dirContent = (directory_entry_t*) data;
//calcule du nombre d entrees a partir de la taille
int numberOfEntries = (inode.size/sizeof(directory_entry_t))+1;
//iteration sur les entrees du repertoire pour trouver le token demande
for(int i = 0 ; i < numberOfEntries; i++)
{
    if( strcmp(dirContent[i].name,entryName) == 0 )
    {
        //nous avons trouve l entree, on retourne son index
        return dirContent[i].index;
    };
}
//si je suis ici c est que le token n existe pas dans le repertoire
osErrno = E_NO_SUCH_FILE;
return -1;
}




// Fonction elementaire pour liberer un bloc donnee
int _free_databloc(int index)
{
    databloc_bitmap_t dbmap;
    _loadDBMap((char*)&dbmap.map);
    _setpos((char*)&dbmap.map, index, 0 );
    _writeDBMap((char*)&dbmap.map);
    return 0;
};

//fonction elementaire pour avoir un nouveau bloc donnees
int _allocate_new_databloc()
{
 return _find_take_free_databloc();
};



/*copie tout le contenu du fichier dans ptr*/

int _copy_file_content(void* ptr, const inode_bloc_t* inode_ptr)
{
    //ici simple utilisation de la fonction existante
    int toRead = inode_ptr->size;
    _read_file_content(ptr,0,toRead,inode_ptr);
    return 0;
};



int _getinodeByNumber(const int num, inode_bloc_t* ptr)
{
    //calcule simple pour transformer les coordonnees correctement
  int ind = num / 4;  //indice du bloc
  int p = num % 4;    // indice interne
  inode_bloc_t sect_in[4]; //bloc complet d'inodes
  //lecture a partir de l offset des inodes
  if  (Disk_Read(INODE_OFFSET+ind, (char *) sect_in)  == -1 ) {
    perror("Disk_Read() failed\n");
    osErrno = E_GENERAL;
    return -1;
    }
  //copie du resultat car nous avons recupere trop d informations
  memcpy(ptr,sect_in+p,sizeof(inode_bloc_t));
  return 0;
};

int _setinodeByNumber(const int num, inode_bloc_t* ptr)
{
  int ind = num / 4;  //indice du bloc
  int p = num % 4;    // indice interne
  inode_bloc_t sect_inout[4]; //bloc complet d'inodes
  //lecture des anciennes valeurs
  //c est important car il ne faut pas ecraser les valeurs des inodes voisins
  if  (Disk_Read(INODE_OFFSET+ind, (char *) sect_inout)  == -1 ) {
    perror("Disk_Read() failed\n");
    osErrno = E_GENERAL;
    return -1;
    }
  //copie des donnes uniquement dans l inode qui nous interesse et ne pas toucher aux autres
  memcpy(sect_inout+p,ptr,sizeof(inode_bloc_t));

    //ecriture de l ensemble maintenant sur le disque
  if  (Disk_Write(INODE_OFFSET+ind, (char *) sect_inout)  == -1 ) {
    perror("Disk_Write() failed\n");
    osErrno = E_GENERAL;
    return -1;
    }
// tout est ok
  return 0;
};


//fonction pour formatee le disque
int _createAndFormatNewDisc()
{
    if( formatDisc() == -1)
    {
        perror("Error on format disc");
        return -1;
    } ;
    return 0;
}



//fonction utile pour trouver le repertoire contenant dans un chemin
int _copy_trim_last_token(char* destPath, const char* srcPath)
{
    if(srcPath[0] != '/')
    {
        perror("Absolute path required");
        return -1;
    }
    strcpy(destPath,srcPath);
    int i = strlen(destPath);
    for(; (destPath[i] != '/') && (i >=0) ; i--)
    {
        //
    }
    if(i == 0) // cas de la racine
    {
        destPath[i+1] = '\0';
        return 0;
    }else
    {
        //autre cas nous coupons a patir de caractere /
        destPath[i] = '\0';
        return 0;
    }

}


//avoir le dernier token
int _get_last_token(char* dest, const char* path)
{
    int i = strlen(path);
    char localname[MAX_NAME_SIZE];
    int j = 0;
    for(; (path[i] != '/') && (i >=0) && j < MAX_NAME_SIZE; i--, j++)
    {
        localname[j] = path[i];
    }
    localname[j] = '\0';
    int k=0;
    for( ; k < j;k++)
    {
        dest[k] = localname[j-k-1];
    }
    dest[k] = '\0';
    return 0;
}

//copie du chemin path le path contenant et le nom du fichier
int _get_containing_path_and_lastname(char* containingpath, char* lastToken, const char* path)
{
    if(_get_last_token(lastToken,path) != 0) return -1;
    if(_copy_trim_last_token(containingpath,path) != 0) return -1;
    return 0;
}


int _readbit(char c, int pos) {
  char conv[]={0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40 , 0x80 };

  if (pos < 0 || pos > 7) {
    perror("bit: incorrect position \n");
    osErrno = E_GENERAL;
    return -1;
    }
    c = c & conv[pos];
    if (c == '\0')
    return 0;
    else
    return 1;
}

int _setbit(char* c, int pos, int val) {
  //char conv[]={0x80, 0x40,  0x20, 0x10,  0x08, 0x04, 0x02, 0x01};
  //char anticonv[]={0x7F, 0xBF, 0xDF, 0xEF, 0xF7, 0xFB, 0xFD, 0xFE};
  char conv[]={0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40 , 0x80 };
  char anticonv[]={0xFE, 0xFD, 0xFB,  0xF7, 0xEF, 0xDF, 0xBF, 0x7F  };
  if (pos < 0 || pos > 7) {
    perror("bit: incorrect position \n");
    osErrno = E_GENERAL;
    return -1;
    }
  if (val < 0 || val > 1) {
    perror("bit: incorrect value \n");
    osErrno = E_GENERAL;
    return -1;
    }
    if (val == 1)
        *c = (*c) | conv[pos];
    else
        *c = (*c) & anticonv[pos];
    return 0;
}



int  _readpos(char * map, int pos)  // retourne le bit M[pos]
{
  if (pos < 0 || pos > 8191) {
    perror("bit: incorrect position \n");
    osErrno = E_GENERAL;
    return -1;
    }
  int ind = pos / 8;
  int p = pos % 8;
   return _readbit(map[ind],p);
}




int  _setpos(char * map, int pos, int val)
{
  if (pos < 0 || pos > 8191) {
    perror("bit: incorrect position \n");
    osErrno = E_GENERAL;
    return -1;
    }
  if (val < 0 || val > 1) {
    perror("bit: incorrect value \n");
    osErrno = E_GENERAL;
    return -1;
    }
  int ind = pos / 8;
  int p = pos % 8;
   return _setbit(map+ind,p,val);
}

int _findfreeFromMap(char * map)
{
  int i;
  for(i=0; i < 8192; i++) {
  if (_readpos(map,i)==0)  break;
  }
  if (i == 8192)
  {
    perror("bitmap is full \n");
    osErrno = E_GENERAL;
    return -1;
    }
   return i;
}


int _loadInodeMap(char* map)
{
    if ( (Disk_Read(1, map)  == -1) || (Disk_Read(2, map+512)  == -1) ) {
    perror("Disk_Read() Imap failed\n");
    osErrno = E_GENERAL;
    return -1;
    }
    return 0;
}


int _loadDBMap(char* map)
{
    if ( (Disk_Read(3, map)  == -1) || (Disk_Read(4, map+512)  == -1) ) {
    perror("Disk_Read() Imap failed\n");
    osErrno = E_GENERAL;
    return -1;
    }
    return 0;
}

int _writeDBMap(const char* map)
{
    if ( (Disk_Write(3, (char*) map)  == -1) || (Disk_Write(4, (char*)map+512)  == -1) ) {
    perror("Disk_Write() DB failed\n");
    osErrno = E_GENERAL;
    return -1;
    }
    return 0;
}

int _writeInodeMap(const char* map)
{
    if ( (Disk_Write(1, (char*) map)  == -1) || (Disk_Write(2, (char*)map+512)  == -1) ) {
    perror("Disk_Write() DB failed\n");
    osErrno = E_GENERAL;
    return -1;
    }
    return 0;
}



int _find_free_databloc()
{
    char map[1024];
    _loadDBMap(map);
    return _findfreeFromMap(map);
}



int _find_take_free_databloc()
{
    databloc_bitmap_t dbmap;
    _loadDBMap((char*)&dbmap.map);
    int i = _findfreeFromMap((char*)&dbmap.map);
    if(i == -1)
    {
        perror("Error to find free bloc");
        return -1;
    }
    _setpos((char*)&dbmap.map, i, 1);
    _writeDBMap((char*)&dbmap.map);
    return i;
}

int _find_take_free_inode()
{
    inode_bitmap_t inmap;
    _loadInodeMap((char*)&inmap.map);
    int i = _findfreeFromMap((char*)&inmap.map);
    if(i == -1)
    {
        perror("Error to find free bloc");
        return -1;
    }
    _setpos((char*)&inmap.map, i, 1);
    _writeInodeMap((char*)&inmap.map);
    return i;
}


int _create_new_directory_entry(int index, const char* newEntryName, int entryType)
{
    inode_bloc_t inode; // une buffer de travail
    inode_bloc_t inode_orig; // une copie propre

    if( _getinodeByNumber(index, &inode) != 0)
    {
        return -1;
    };

    if( _getinodeByNumber(index, &inode_orig) != 0)
    {
        return -1;
    };

    int taille = inode.size;
    int nbBloc = 0;
    if(taille != 0)
    {
    nbBloc  = (taille/sizeof(data_bloc_t))+1;//nbre de bloc deja utilise
    }
    else {
    //sinon c est une initialisation nbBloc = 0, ici pour confirmation
    nbBloc = 0;
    }
    //Verifier si nous avons besoin d un nouveau bloc de donnee
    if(taille + sizeof(directory_entry_t) > nbBloc*sizeof(data_bloc_t))
    {
        //beson d un nouveau pointeur vers un bloc donnee
        if( nbBloc < 30) // je verifie que je n utilise pas plus de 30 ptrs
        {
        //ici je dois allouer un nouveau bloc donnee pour le repertoire
        int indexDB = _find_take_free_databloc();
        if(indexDB == -1)
        {
            perror("No free databloc");
            return -1;
        }
        inode.pointers[nbBloc] = indexDB;

        }
        else
        {
            perror("Max size for a directory is reached");
            return -1;
        }
    }
    //j augmente la taille avec la nouvelle entree
    inode.size = inode.size + sizeof(directory_entry_t);
    //lecture des entrees du repertoire
    char* dirContent = alloca(inode.size);

    _copy_file_content(dirContent,&inode);
    //verification que la cle n existe pas

    //verifier que le nom n existe pas deja
    if ( _exists_key(dirContent, newEntryName, inode.size) )
    {
        perror("Key already exists ");
        return -1;
    }


    //maintenant je me place a l ancien index pour ecrire la nouvelle entree
    char* writeIndex = dirContent+inode_orig.size; // attention je regarde ma copie propre
    directory_entry_t* writeIndexDir = (directory_entry_t*) writeIndex;
    int newdirindex = -1;

    if(entryType == DIRECTORY_TYPE)
    {
        newdirindex = _create_new_directory_inode();
    }
    else
    {
        newdirindex = _create_new_file_inode();
    }
    if(newdirindex == -1)
    {
        perror("error on _create_new_directory_inode");
        return -1;
    }
    writeIndexDir->index = newdirindex;

    strcpy( writeIndexDir->name, newEntryName);
    if( _write_file_content(dirContent,inode.size,&inode) == -1)
    {
        perror("Error on write content of directory");
        return -1;
    }

    //le contenu du repertoire est copie maintenant je mets a jour l inode du repertoire
    _setinodeByNumber(index,&inode);
    return 0;
};

int _create_new_inode(int type)
{
    int index = _find_take_free_inode();
    if(index == -1)
    {
        perror("Cannot find a new inode to create the directory");
        return -1;
    }
    //load the inode table and set is to the correct values
    int ind = index / 4; //num de secteur
    int p = index % 4;
    Sector sect_inout;
    if  (Disk_Read(INODE_OFFSET+ind, (char *) &sect_inout.data)  == -1 ) {
    perror("Disk_Read() failed\n");
    osErrno = E_GENERAL;
    return -1;
    }
    inode_bloc_t inode_local;
    inode_local.type = type;
    inode_local.size = 0;
    memset(inode_local.pointers,-1,30*sizeof(int));

    memcpy(((inode_bloc_t *) &sect_inout.data)+p,&inode_local,sizeof(inode_bloc_t));

    if  (Disk_Write(INODE_OFFSET+ind, (char *) &sect_inout.data)  == -1 ) {
    perror("Disk_Write() failed\n");
    osErrno = E_GENERAL;
    return -1;
    }
    return index;
};

//demande d une inode libre
int _create_new_directory_inode()
{
    return _create_new_inode(DIRECTORY_TYPE);
};
int _create_new_file_inode()
{
    return _create_new_inode(FILE_TYPE);
};

//--//
int _path_2_inode(const char* path, inode_bloc_t* ptr, int* retIndex)
{
    int  index =_path_2_inode_number(path);
    if(index == -1) {return -1;}
    if(retIndex != NULL) {*retIndex = index;}

    return _getinodeByNumber(index, ptr);
}

int _find_take_free_descriptor()
{
    int i = 0;
        for (i = 0 ; i < MAX_OPEN_FILES; i++)
        {
            if(!_open_file_table[i].used)
            {
                _open_file_table[i].used = 1;
                return i;
            }
        }
    return -1 ;
}



//**************88
/* Section des fonctions principales
 */
//**************88

//sync du disque
int
FS_Sync()
{
    //FS_Syn
    if( Disk_Save(fileName) == -1)
    {
        perror("FS_Sync()");
        return -1;
    };
    return 0;
}



//fonction de boot
int
FS_Boot(char *path)
{
    printf("FS_Boot %s\n", path);
    fileName = path;
    if (Disk_Init() == -1) {
    perror("Disk_Init() failed\n");
    osErrno = E_GENERAL;
    return -1;
    }

    //Chargement du fichier image
    if ( Disk_Load(path) == -1)
    {
        //Disk_Load() failed;
        if(diskErrno == E_OPENING_FILE) // si le fichier n existe pas je dois le creer
        {
            if( _createAndFormatNewDisc() == -1 )
            {
                osErrno = E_GENERAL;
                return -1;
            };
        }
        else
        {
        osErrno = E_GENERAL;
        return -1;
        }
    }
    //disque est charge dans la memoire
    //verification du magic number
    superblock_t buffer;
    Disk_Read(0, (char*) &buffer);
    if(buffer.magicnumber == MAGICNUMBER)
    {
        // Image disque OK
        return 0;
    }
    else
    {
        perror("Erreur dans l'image disque");
        return -1;
    }

}



//fonction deleguee pour creation d un repertoire
// directory ops
int Dir_Create(char *path) //PP
{
    char* pathcopy = alloca(strlen(path)+1);
    if ( _copy_trim_last_token(pathcopy,path) != 0)
    {
        perror("Error on path creation");
        return -1;
    };

    int index = _path_2_inode_number(pathcopy);
    if(index == -1)
    {
        perror("cannot find directory\n");
        osErrno = E_NO_SUCH_FILE;
        return -1;
    }


    //ajout d une nouvelle entree de type repertoire dans cette inode
    char newEntryName[MAX_NAME_SIZE];
    _get_last_token(newEntryName,path);
    _create_new_directory_entry(index,newEntryName, DIRECTORY_TYPE);
    return 0;
}



int
Dir_Size(char *path)
{
    int index = _path_2_inode_number(path);
    if(index == -1)
    {
        return -1;
    }
    inode_bloc_t bloc;
    if(_getinodeByNumber(index,&bloc) == -1)
    {
        return -1;
    };
    if( bloc.type != DIRECTORY_TYPE)
    {
        return -1;
    };
    return bloc.size;
}

int Dir_Read(char *path, void *buffer, int size)
{
    //Dir_Read
    int index = _path_2_inode_number(path);
    if(index == -1)
    {
        return -1;
    }
    //get directory inode
    inode_bloc_t inode;
    if( _getinodeByNumber( index , &inode ) == -1)
    {
    osErrno = E_NO_SUCH_FILE;
    return -1;
    }
    if( inode.type != DIRECTORY_TYPE)
    {
        osErrno = E_GENERAL;
        return -1;
    }
    if(inode.size > size)
    {
        osErrno = E_BUFFER_TOO_SMALL;
        return -1;
    }
    //lecture du contenu du repertoire pour cherche le nom
    //allocation de la memoire pour recevoir localement tout le contenu
    char* data = alloca(inode.size);
    /*copier tout le contenu pour travailler localement*/
    if( _copy_file_content(data,&inode) == -1) //
    {
    osErrno = E_GENERAL;
    return -1;
    }
    memcpy(buffer,data,inode.size);
    return (inode.size/sizeof(directory_entry_t));
}




int
Dir_Unlink(char *path)
{
    int size = Dir_Size(path);
    if(size != 0)
    {
        osErrno = E_DIR_NOT_EMPTY;
        return -1;
    }
    //Contenant ici
    char* pathContenant = alloca(strlen(path));
    char name[16];
    inode_bloc_t inode;
    int indexContenant;

    _copy_trim_last_token(pathContenant,path);
    int sizeContenant = Dir_Size(pathContenant);
    char* buffer = alloca(sizeContenant);
    _path_2_inode(pathContenant,&inode,&indexContenant);

    _get_last_token(name,path);

    int num = Dir_Read(pathContenant,buffer,sizeContenant);
    int i = 0;

    for(i = 0; i < num; i++)
    {
        if(strcmp( ( ((directory_entry_t*) buffer)+i)->name, name ) == 0)
        {
        (((directory_entry_t*) buffer)+i)->name[0] = '\0';
        //il faut liberer inode
        _free_inode((((directory_entry_t*) buffer)+i)->index);
        (((directory_entry_t*) buffer)+i)->index = -1;
        break;
        }
    }
    //ecriture du contenu du fichier maintenant
    _write_file_content(buffer,inode.size,&inode);
    //inode du repertoire n a pas change donc pas besoin de la reecrire, juste le contenu avec la meme taille
    return 0;
}




//done
int
File_Open(char *file)
{
    int inodeindex = _path_2_inode_number(file);
    if ( inodeindex == -1)
    {
        osErrno = E_NO_SUCH_FILE;
        return -1;
    }
    if(!_is_open_filetable_init) _init_open_file_table();
    int des = _find_take_free_descriptor() ;
    if ( des == -1)
    {
        osErrno = E_TOO_MANY_OPEN_FILES;
        return -1;
    }
    _open_file_table[des].read_write_index = 0;
    _open_file_table[des].inode_index = inodeindex;
    return des;
}





//
int
File_Create(char *file)
{
    char filename[16];
    char* containingPath = alloca(strlen(file)+1);
    //avoir le chemin contenant et le nom du fichier a creer
    if(
    _get_containing_path_and_lastname(containingPath, filename, file)!= 0
    )
    {
        return -1;
    }
    //avoir l inode du repertoire contenant
    int inodeContainingDir = -1;
    inode_bloc_t inode;

    if(
    _path_2_inode(containingPath,&inode, &inodeContainingDir)!= 0
    )
    {
        return -1;
    }


    //ajout une nouvelle entree dans le contenu du repeortoire contenant
    if (_create_new_directory_entry(inodeContainingDir,filename, FILE_TYPE))
    {
        perror("Cannot create a new file");
        return -1;
    }
    return 0;
}

//read
int
File_Read(int fd, void *buffer, int size)
{
    // verifier que fd pointe sur une inode ouverte
    if( fd >= 0 && fd < MAX_OPEN_FILES && _open_file_table[fd].used && _open_file_table[fd].inode_index != -1  )
    {
            //lecture du contenu du fichier a partir du numero d inode
            inode_bloc_t inode;
            if(_getinodeByNumber(_open_file_table[fd].inode_index, &inode) == -1)
            {
                return -1;
            }
            int ret = _read_file_content(
                buffer,
                _open_file_table[fd].read_write_index,_open_file_table[fd].read_write_index+size,
                &inode
                );

            if(ret == -1)
            {
                perror("File Read");
                return -1;
            }
            _open_file_table[fd].read_write_index = _open_file_table[fd].read_write_index+size;
            return ret;
    }
    else
    {
        perror("File Read");
        return -1;
    }

}

int _max(int i, int j)
{
    return (i<j)? j : i;
}

int
File_Write(int fd, void *buffer, int size)
{

    if( fd >= 0 && fd < MAX_OPEN_FILES && _open_file_table[fd].used && _open_file_table[fd].inode_index != -1  )
    {
        inode_bloc_t inode;
        if(_getinodeByNumber(_open_file_table[fd].inode_index, &inode) == -1)
        {
                return -1;
        }
        //copie du contenu du fichier
        int newSize = _max(_open_file_table[fd].read_write_index+size, inode.size) ;

        char* oldContentBuffer = alloca(newSize);

        int ret = _read_file_content(oldContentBuffer,0,inode.size,&inode); //lecture uniquement de inode.size

        if(ret == -1)
            {
                perror("File Write");
                return -1;
            }

        memcpy(oldContentBuffer+_open_file_table[fd].read_write_index, buffer,size );

        _open_file_table[fd].read_write_index = _open_file_table[fd].read_write_index + size;
        int wret = 0;
        wret = _write_file_content(oldContentBuffer,newSize,&inode);

        if( wret == -1)
        {
            return -1;
        };
        //mise a jour de l inode
        if(_setinodeByNumber(_open_file_table[fd].inode_index,&inode))
        {
                return -1;
        };
        return wret;
    }
    else
    {
        perror("FS Write");
        return -1;
    }
}

int
File_Seek(int fd, int offset)
{
    if( fd >= 0 && fd < MAX_OPEN_FILES && _open_file_table[fd].used && _open_file_table[fd].inode_index != -1  )
    {
        _open_file_table[fd].read_write_index = offset;
        return 0;
    }
    else
    {
        return -1;
    }
}

int
File_Close(int fd)
{
   if( fd >= 0 && fd < MAX_OPEN_FILES && _open_file_table[fd].used && _open_file_table[fd].inode_index != -1  )
    {
        _open_file_table[fd].read_write_index = 0;
        _open_file_table[fd].used = 0;
        _open_file_table[fd].inode_index = -1;
        return 0;
     }
    else
    {
        return -1;
    }
}


//***
//** DEBUT DES FONCTIONS A FAIRE DU CHAPITRE 1 BIS
//***

//TODO
//Verifier si key est presente dans le contenu du repertoire pointe par keychain
// keychain: tableau de directory_entry_t de taille 'size' octets
// key: le nom recherche
int _exists_key(const char* keychain, const char* key, int size)
{
      size_t find = 0;
      int i = 0;
      while(!find && i<size){
        if(strcmp(keychain[i], key)){
          find = 1;
        }
      }

    return find;
}

//TODO
// librer l inode a partir de son index
int _free_inode(int ide)
{
    printf("_free_inode: Not Implemented\n");
    return 0;
}


//TODO
// Effacer le fichier du repertoire
int
File_Unlink(char *file) //Source from github (https://github.com/m30m/os-fs/blob/master/LibFS.c)
{
  printf("File_Unlink\n");

  struct inode *parent;
  struct inode *node;
  int parent_inode_number = find_last_parent(path, &parent);
  int inode_number = find_inode(path, &node);
  if (parent_inode_number == -1 || inode_number == -1)
  {
      osErrno = E_NO_SUCH_FILE;
      return -1;
  }
  if (inode_open_count[inode_number] > 0)
  {
      osErrno = E_FILE_IN_USE;
      free(parent);
      free(node);
      return -1;
  }

  if (node->type == DIR_TYPE)
  {
      fprintf(stderr, "Use dir unlink for directories\n");
      return -1;
  }

  char *tmp = malloc(SECTOR_SIZE);
  struct file_record *tmp_file_record = malloc(sizeof(struct file_record));

  int i;
  for (i = 0; i < DATA_BLOCK_PER_INODE; i++)
  {
      if (node->data_blocks[i] != 0)
      {
          set_datablock_bitmap(node->data_blocks[i], 0);
      }
  }

  set_inode_bitmap(inode_number, 0);

  char found_entry_to_remove = 0;
  for (i = 0; i < DATA_BLOCK_PER_INODE && !found_entry_to_remove; i++)
  {
      if (parent->data_blocks[i] != 0)
      {
          Disk_Read(parent->data_blocks[i], tmp);
          int j;
          for (j = 0; j < SECTOR_SIZE / sizeof(struct file_record); j++)
          {
              memcpy(tmp_file_record, &tmp[j * sizeof(struct file_record)], sizeof(struct file_record));
              if (tmp_file_record->inode_number == inode_number)
              {
                  tmp_file_record->inode_number = 0;
                  memset(tmp_file_record->name, 0, sizeof(tmp_file_record->name));
                  memcpy(&tmp[j * sizeof(struct file_record)], tmp_file_record, sizeof(struct file_record));
                  Disk_Write(parent->data_blocks[i], tmp);
                  char is_whole_block_empty = 1;
                  int k;
                  for (k = 0; k < SECTOR_SIZE / sizeof(struct file_record); k++)
                  {
                      memcpy(tmp_file_record, &tmp[k * sizeof(struct file_record)], sizeof(struct file_record));
                      if (tmp_file_record->inode_number != 0)
                      {
                          is_whole_block_empty = 0;
                          break;
                      }
                  }
                  if (is_whole_block_empty)
                      set_datablock_bitmap(parent->data_blocks[i], 0);
                  found_entry_to_remove = 1;
                  break;
              }

          }
      }
  }
  free(node);
  free(parent);
  free(tmp);
  free(tmp_file_record);
  return 0;
}


/* TODO
 * Lecture du contenu d un fichier represente par inode_ptr a partir l octet start jusqu a end
 * le buffer doit contenir assez d espace pour recevoir les donnees
 */
int _read_file_content(char* buffer, int start, int end, const inode_bloc_t* inode_ptr)
{

}


//TODO
// ecrire simplement le contenu sur le disque a partir de l inode
int _write_file_content(char* buffer, int size, inode_bloc_t* inode_ptr, int offset) // Source from website ()
{
  const uint8_t *buffer = buffer_;
int bytes_written = 0;
uint8_t *bounce = NULL;

if (inode->deny_write_cnt)
 return 0;

while (size > 0)
 {
   /* Sector to write, starting byte offset within sector. */
   block_sector_t sector_idx = byte_to_sector (inode, offset);
   int sector_ofs = offset % DATA_BLOCK_PER_INODE;

   /* Bytes left in inode, bytes left in sector, lesser of the two. */
   int inode_left = inode_length (inode) - offset;
   int sector_left = DATA_BLOCK_PER_INODE - sector_ofs;
   int min_left = inode_left < sector_left ? inode_left : sector_left;

   /* Number of bytes to actually write into this sector. */
   int chunk_size = size < min_left ? size : min_left;
   if (chunk_size <= 0)
     break;

   if (sector_ofs == 0 && chunk_size == DATA_BLOCK_PER_INODE)
     {
       /* Write full sector directly to disk. */
       block_write (fs_device, sector_idx, buffer + bytes_written);
     }
   else
     {
       /* We need a bounce buffer. */
       if (bounce == NULL)
         {
           bounce = malloc (DATA_BLOCK_PER_INODE);
           if (bounce == NULL)
             break;
         }

       /* If the sector contains data before or after the chunk
          we're writing, then we need to read in the sector
          first.  Otherwise we start with a sector of all zeros. */
       if (sector_ofs > 0 || chunk_size < sector_left)
         block_read (fs_device, sector_idx, bounce);
       else
         memset (bounce, 0, DATA_BLOCK_PER_INODE);
       memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
       block_write (fs_device, sector_idx, bounce);
     }

   /* Advance. */
   size -= chunk_size;
   offset += chunk_size;
   bytes_written += chunk_size;
 }
free (bounce);

return bytes_written;
}
