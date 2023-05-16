#include "fat.h"
#include "ds.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define SUPER 0
#define TABLE 2
#define DIR 1

#define SIZE 1024

// the superblock
#define MAGIC_N           0xAC0010DE
typedef struct{
	int magic;
	int number_blocks;
	int n_fat_blocks;
	char empty[BLOCK_SIZE-3*sizeof(int)];
} super;

super sb;

//item
#define MAX_LETTERS 6
#define OK 1
#define NON_OK 0
typedef struct{
	unsigned char used;
	char name[MAX_LETTERS+1];
	unsigned int length;
	unsigned int first;
} dir_item;

#define N_ITEMS (BLOCK_SIZE / sizeof(dir_item))
dir_item dir[N_ITEMS];

// table
#define FREE 0
#define EOFF 1
#define BUSY 2
unsigned int *fat;

int mountState = 0;

int fat_format(){ 
	//Verificando primeiro se disco está montado
	if(mountState == 1)
	{
		return -1;
	}

	//Criando buffer do tamanho de um bloco
	char *buffer = malloc(BLOCK_SIZE); 

	//Criando struct do superbloco
	sb.magic = MAGIC_N;
	sb.number_blocks = ds_size();
	sb.n_fat_blocks = 1;

	//Copiando struct para o buffer
	memcpy(buffer, &sb, sizeof(sb));

	//Escrevendo superbloco no disco 
	ds_write(0, buffer);

	//Limpando lixo do buffer
	memset(buffer, 0, BLOCK_SIZE);

	//Inicializando DIR sem arquivos
	for(int i = 0; i < N_ITEMS; i++)
	{
		dir[i].used = NON_OK;
	}

	memcpy(buffer, dir, sizeof(dir));

	//Escrevendo DIR no disco 
	ds_write(1, buffer);

	memset(buffer, 0, BLOCK_SIZE);

	//Inicializando FAT sem arquivos
	fat[SUPER] = BUSY;
	fat[DIR] = BUSY;
	fat[TABLE] = BUSY;

	for(int i = 3; i < sb.number_blocks; i++)
	{
		fat[i] = FREE;
	}

	memcpy(buffer, fat, sizeof(fat));

	//Escrevendo FAT no disco
	ds_write(2, buffer);

	//Desalocando memória
	free(buffer);

  	return 0;
}

//Função que verifica se um item existe num array
int contains(int array[], int lenght, int value) {
    for (int i = 0; i < lenght; i++) {
        if (array[i] == value) {
            return i;
        }
    }
    return -1;
}

//Lê a FAT
void read_fat()
{
	fat = (unsigned int *) malloc(BLOCK_SIZE);

	ds_read(2, (char *) fat);
}

//Busca os blocos de um arquivo na fat com base no primeiro bloco
void search_file_blocks_on_fat(unsigned int first, unsigned int* blocks, unsigned int* fatDebug)
{
	//O primeiro sempre fará parte da lista de blocos
	blocks[0] = first;
	//Começando em 1 pois já iniciamos a pos 0
	int blocks_counter = 1;
	unsigned int block_value = fatDebug[first];
	
	//Quando chegar em 1 atingimos o fim do arquivo

	while(block_value != 1)
	{
		//Ao menos um bloco do arquivo está apontando para um bloco vazio ou busy
		if(block_value < 1 || block_value == 2)
		{
			blocks[0] = -1;	
			break;
		}	

		blocks[blocks_counter] = block_value;

		block_value = fatDebug[block_value];
		blocks_counter++;
	}

	//Indica fim do array de blocos
	blocks[blocks_counter] = -1;
}

void fat_debug(){
	//Criando buffer do tamanho de um bloco
	char *buffer = malloc(BLOCK_SIZE);
	super sbDebug;

	//========================LENDO INFORMAÇÕES REFERENTES AO SUPER BLOCO========================
	//Lendo bloco 0 e guardando no buffer
	ds_read(0, buffer);
	//Copiando conteúdo do buffer para struct
	memcpy(&sbDebug, buffer, sizeof(super));
	//Definindo status do Magic number
	char mgc_status[10] = "is ok";
	//Se o número mágico lido for diferente do definido, há erros
	if(sbDebug.magic != MAGIC_N)
	{
		strcpy(mgc_status, "is not ok");
	}

	//Printando informações do superbloco
	printf("\nsuperblock:");
	printf("\n   magic %s", mgc_status);
	printf("\n   %d blocks", sbDebug.number_blocks);
	printf("\n   %d block fat\n", sbDebug.n_fat_blocks);
	//===========================================================================================

	//Lendo FAT
	unsigned int *fatDebug;
	fatDebug = (unsigned int *) malloc(BLOCK_SIZE);

	ds_read(2, (char *) fatDebug);

	//========================LENDO INFORMAÇÕES REFERENTES A ARQUIVOS NO DIR=====================
	//Lendo bloco 1 e guardando no buffer
	ds_read(1, buffer);

	unsigned int* file_blocks;

	//O máximo de blocos que um arquivo pode ocupar é 17 (a quantidade de blocos para arquivos)
	file_blocks = (unsigned int *) malloc(N_ITEMS-3);

	dir_item dirDebug[N_ITEMS];
	for(int i = 0; i < N_ITEMS; i++)
	{
		memcpy(&dirDebug[i], &buffer[i*sizeof(dir_item)], sizeof(dir_item));

		//Se o dígito verificador é 0, a entrada em DIR está livre
		if((int) dirDebug[i].used == NON_OK)
		{
			continue;
		}

		search_file_blocks_on_fat(dirDebug[i].first, file_blocks, fatDebug);

		printf("\nFile: \"%s\":", dirDebug[i].name);
		printf("\n   size: \"%u\" bytes", dirDebug[i].length);
		printf("\n   Blocks:");

		int i = 0;
		while(file_blocks[i] != -1)
		{
			printf(" %u", file_blocks[i]);
			i++;
		}
		printf("\n");

		memset(file_blocks, 0, N_ITEMS-3);
	}

	free(file_blocks);
	free(fatDebug);
	free(buffer);
}

int fat_mount(){
	//Criando buffer do tamanho de um bloco
	char *buffer = malloc(BLOCK_SIZE); 

	//Lendo bloco 0 e guardando no buffer
	ds_read(0, buffer);
	//Copiando conteúdo do buffer para struct
	memcpy(&sb, buffer, sizeof(super));

	if(sb.magic != MAGIC_N)
	{
		return -1;
	}

	//Limpando lixo do buffer
	memset(buffer, 0, BLOCK_SIZE);

	//Lendo bloco 1 e guardando no buffer
	ds_read(1, buffer);

	memcpy(&dir, buffer, BLOCK_SIZE);

	//Lê a FAT
	read_fat();

	mountState = 1;

  	return 0;
}

int fat_create(char *name){
	if(mountState == 0)
	{
		return -1;
	}

	for(int i = 0; i < N_ITEMS; i++)
	{
		//Verificando se a entrada do dir está livre
		if((int) dir[i].used == OK)
		{
			continue;
		}

		//Procurando entrada livre na FAT
		for(int i = 3; i < sb.number_blocks; i++)
		{
			if(fat[i] == BUSY || fat[i] == EOFF)
			{
				continue;
			}


			fat[i] == EOFF;
			dir[i].used == NON_OK;
			strcpy(dir[i].name, name);
			dir[i].first = i;

			//Criando buffer do tamanho de um bloco
			char *buffer = malloc(BLOCK_SIZE); 

			memcpy(buffer, dir, sizeof(dir));

			//Escrevendo DIR no disco 
			ds_write(1, buffer);

			//Limpando lixo do buffer
			memset(buffer, 0, BLOCK_SIZE);
			
			//Copiando struct para o buffer
			memcpy(buffer, fat, sizeof(fat));

			//Escrevendo FAT no disco 
			ds_write(2, buffer);

			free(buffer);

			return 0;
		}

		return -1;
	}

  	return -1;
}

int fat_delete( char *name){
  	return 0;
}

int fat_getsize( char *name){ 
	return 0;
}

//Retorna a quantidade de caracteres lidos
int fat_read( char *name, char *buff, int length, int offset){
	return 0;
}

//Retorna a quantidade de caracteres escritos
int fat_write( char *name, const char *buff, int length, int offset){
	return 0;
}
