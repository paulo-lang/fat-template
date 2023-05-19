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
	if(mountState == 0)
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
	memcpy(buffer, &sb, BLOCK_SIZE);

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

	memcpy(buffer, fat, BLOCK_SIZE);

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
		//lendo uma entrada de dir
		memcpy(&dirDebug[i], &buffer[i*sizeof(dir_item)], sizeof(dir_item));

		//Se o dígito verificador é 0, a entrada em DIR está livre
		if((int) dirDebug[i].used == NON_OK)
		{
			continue;
		}

		//pegando blocos do arquivo
		search_file_blocks_on_fat(dirDebug[i].first, file_blocks, fatDebug);

		//printando infos da entrada do dir
		printf("\nFile: \"%s\":", dirDebug[i].name);
		printf("\n   size: \"%u\" bytes", dirDebug[i].length);
		printf("\n   Blocks:");

		//-1 significa fim do array então até lá printamos os blocos
		int i = 0;
		while(file_blocks[i] != -1)
		{
			printf(" %u", file_blocks[i]);
			i++;
		}
		printf("\n");

		//Limpando conteúdo do array de file_blocks
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
			if(fat[i] != FREE)
			{
				continue;
			}

			fat[i] = EOFF;
			dir[i].used = OK;
			strcpy(dir[i].name, name);
			dir[i].first = i;
			dir[i].length = 0;

			//Criando buffer do tamanho de um bloco
			char *buffer = malloc(BLOCK_SIZE); 

			memcpy(buffer, dir, sizeof(dir));

			//Escrevendo DIR no disco 
			ds_write(1, buffer);

			//Limpando lixo do buffer
			memset(buffer, 0, BLOCK_SIZE);
			
			//Copiando struct para o buffer
			memcpy(buffer, fat, BLOCK_SIZE);

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
	if(mountState == 0)
	{
		return -1;
	}

	for(int i = 0; i < N_ITEMS; i++)
	{
		//verificando se arquivo tem o nome buscado
		if(strcmp(name, dir[i].name) != 0)
		{
			continue;
		}

		//iniciando variável de posição
		unsigned int pos = dir[i].first;
		//Iniciando array que guardará posições da FAT a serem marcadas como livres
		unsigned int *file_positions;
		//O máximo de blocos que um arquivo pode ocupar é 17 (a quantidade de blocos para arquivos)
		file_positions = (unsigned int *) malloc(N_ITEMS-3);
		int counter = 0;

		//Enquanto não achar a última posição continuamos salvando as posições encontradas no array
		while(pos != 1)
		{
			file_positions[counter] = pos;
			pos = fat[pos];
			counter++;
		}

		//Limpando posições encontradas
		for(int j = 0; j < counter; j++)
		{
			fat[file_positions[j]] = FREE;
		}

		//Definindo entrada do dir como livre
		dir[i].used = NON_OK;

		//Criando buffer do tamanho de um bloco
		char *buffer = malloc(BLOCK_SIZE); 

		memcpy(buffer, fat, BLOCK_SIZE);
		//Escrevendo FAT no disco
		ds_write(2, buffer);

		memset(buffer, 0, BLOCK_SIZE);

		memcpy(buffer, dir, BLOCK_SIZE);
		ds_write(1, buffer);

		free(buffer);

		return 0;
	}

	return -1;
  	
}

int fat_getsize( char *name){ 
	return 0;
}

//Retorna a quantidade de caracteres lidos
int fat_read( char *name, char *buff, int length, int offset){
	if(mountState == 0)
	{
		return -1;
	}

	int index = -1;
	for(int i = 0; i < N_ITEMS; i++)
	{
		//verificando se arquivo tem o nome buscado
		if(dir[i].used == NON_OK || strcmp(name, dir[i].name) != 0)
		{
			continue;
		}

		index = i;
	}

	if (index == -1) {
       	return -1;
    }

	char *buffer = malloc(BLOCK_SIZE); 
	int bytesRead = 0;
	
	//iniciando variável de posição
	unsigned int pos = dir[index].first;
	//Iniciando array que guardará posições da FAT a serem marcadas como livres
	unsigned int *file_positions;
	//O máximo de blocos que um arquivo pode ocupar é 17 (a quantidade de blocos para arquivos)
	file_positions = (unsigned int *) malloc(N_ITEMS-3);
	search_file_blocks_on_fat(dir[index].first, file_positions, fat);

	int counter = 0;
	int left_on_buff = length;

	ds_read(file_positions[counter], buffer);
	
	int remaining = dir[index].length;

	int blocks_ahead = offset/BLOCK_SIZE;
	counter += blocks_ahead;

	int offset_in_bytes = offset%BLOCK_SIZE;
	
	int to_be_read = 0;

	while(remaining > 0 && file_positions[counter] != -1 && left_on_buff > 0)
	{
		ds_read(file_positions[counter], buffer);
		
		if(remaining >= BLOCK_SIZE) {
			to_be_read = BLOCK_SIZE;
			if(BLOCK_SIZE > left_on_buff)
			{
				to_be_read = left_on_buff;
			}
			memcpy(buff, buffer + offset_in_bytes, to_be_read);
			bytesRead += to_be_read;
			left_on_buff -= to_be_read;
			remaining -= to_be_read;
		} else {
			to_be_read = remaining;
			if(remaining > left_on_buff)
			{
				to_be_read = left_on_buff;
			}
        	memcpy(buff, buffer + offset_in_bytes, to_be_read);
        	bytesRead += to_be_read;
			left_on_buff -= to_be_read;
			remaining -= to_be_read;
    	}
		counter++;
	}
	
	if(bytesRead < length)
	{
		memset(buff + bytesRead, '\0', left_on_buff);
	}

	free(buffer);
	free(file_positions);

	return bytesRead;
}

//Retorna a quantidade de caracteres escritos
int fat_write( char *name, const char *buff, int length, int offset){
	if(mountState == 0)
	{
		return -1;
	}

	int index = -1;
	for(int i = 0; i < N_ITEMS; i++)
	{
		//verificando se arquivo tem o nome buscado
		if(dir[i].used == NON_OK || strcmp(name, dir[i].name) != 0)
		{
			continue;
		}

		index = i;
	}

	if (index == -1) {
       	return -1;
    }

	char *buffer = malloc(BLOCK_SIZE); 
	int bytesWrote = 0;
	
	//iniciando variável de posição
	unsigned int pos = dir[index].first;
	//Iniciando array que guardará posições da FAT a serem marcadas como livres
	unsigned int *file_positions;
	//O máximo de blocos que um arquivo pode ocupar é 17 (a quantidade de blocos para arquivos)
	file_positions = (unsigned int *) malloc(N_ITEMS-3);
	search_file_blocks_on_fat(dir[index].first, file_positions, fat);

	int counter = 0;
	
	int remaining = length;

	int blocks_of_offset = offset/BLOCK_SIZE;
	int offset_in_bytes = offset%BLOCK_SIZE;

	int length_ahead_in_block = length/BLOCK_SIZE;
	int length_ahead_in_bytes = length%BLOCK_SIZE;

	while(file_positions[counter] != -1)
	{
		counter++;
	}

	int blocks_to_allocate = 0;

	if(length_ahead_in_block > 0)
	{
		if(length_ahead_in_bytes > 0)
		{
			blocks_to_allocate++;
		}
	}

	if(length_ahead_in_block > counter)
	{
		blocks_to_allocate += length_ahead_in_block - counter;
	}

	int last_used_block = file_positions[counter-1];

	for(int i = 0; i < blocks_to_allocate; i++)
	{
		int free_block_found = 0;
		for(int j = 3; j < sb.number_blocks; j++)
		{
			if(fat[j] != FREE)
			{
				continue;
			}

			free_block_found = 1;

			fat[j] = EOFF;
			fat[last_used_block] = j;
			last_used_block = j;
			file_positions[counter] = j;
			file_positions[counter+1] = -1;
			counter++;

			break;
		}

		if(!free_block_found)
		{
			remaining -= ((blocks_to_allocate - i)*BLOCK_SIZE) - length_ahead_in_bytes;
		}
	}

	for(int i = 0; i < 10; i++){
		printf("\n=====%u=====\n", fat[i]);
	}

	file_positions[counter] = -1;

	int initial_pos = file_positions[blocks_of_offset];
	counter = blocks_of_offset;
	int to_be_write = remaining;

	if(remaining >= BLOCK_SIZE) {
		to_be_write = BLOCK_SIZE - offset_in_bytes;
		ds_read(initial_pos, buffer);
		memcpy(buffer + offset_in_bytes, buff, to_be_write);
		
		ds_write(initial_pos, buffer);

		bytesWrote += to_be_write;
		remaining -= to_be_write;
	} else {
		to_be_write = remaining;

		ds_read(initial_pos, buffer);
		memcpy(buffer + offset_in_bytes, buff, to_be_write);
		
		ds_write(initial_pos, buffer);

		bytesWrote += to_be_write;
		remaining -= to_be_write;
	}
	counter++;

	while(remaining > 0 && file_positions[counter] != -1)
	{	
		if(remaining >= BLOCK_SIZE) {
			to_be_write = BLOCK_SIZE;
			
			ds_write(file_positions[counter], buff);

			bytesWrote += to_be_write;
			remaining -= to_be_write;
		} else {
			to_be_write = remaining;
	
			ds_read(file_positions[counter], buffer);
			memcpy(buffer, buff, to_be_write);
			memset(buffer + to_be_write, '\0', BLOCK_SIZE - to_be_write);
			
			ds_write(file_positions[counter], buffer);

			bytesWrote += to_be_write;
			remaining -= to_be_write;
		}
		counter++;
	}

	dir[index].length = bytesWrote;

	//Limpando lixo do buffer
	memset(buffer, 0, BLOCK_SIZE);
	//Copiando DIR para o buffer
	memcpy(buffer, dir, sizeof(dir));
	
	//Escrevendo DIR no disco 
	ds_write(1, buffer);

	//Limpando lixo do buffer
	memset(buffer, 0, BLOCK_SIZE);
	//Copiando FAT para o buffer
	memcpy(buffer, fat, BLOCK_SIZE);

	//Escrevendo FAT no disco 
	ds_write(2, buffer);

	free(buffer);
	free(file_positions);

	return bytesWrote;
}
