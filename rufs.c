/*
 *  Copyright (C) 2022 CS416/518 Rutgers CS
 *	RU File System
 *	File:	rufs.c
 *
 */

#define FUSE_USE_VERSION 26
#define F 0
#define D 1
#define RW 0


#include <fuse.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/time.h>
#include <libgen.h>
#include <limits.h>
#include <pthread.h>

#include "block.h"
#include "rufs.h"

pthread_mutex_t mutex;

int totalnumberOfBlks;


char diskfile_path[PATH_MAX];

// Declare your in-memory data structures here

//Stores the actual super block struct information for reference
struct superblock superBlock;

//This is the pointer to block 0 (block containing superblock info) of virtual disk
void * ptrToSuperblock;


//This is the number of inodes that fit into a single block of the inode region
int inodesInBlock = BLOCK_SIZE/sizeof(struct inode);

//This is the number of dirents that fit into a single block of the data region
int directoryEntriesInBlock = BLOCK_SIZE/sizeof(struct dirent);

bitmap_t inodeBitmap;
bitmap_t dataBitmap;

//Helper functions 
void conversionToDirEntries (int blockno);
void bitmapToDisk(char c);
int fileExists(struct inode presentDirectory, const char * fname);



//This function will make the block of block number consist of an array of directory entries after formatting the block
void conversionToDirEntries (int blockNumber){

	//Here memory is allocated for the array of directory entries
	struct dirent * arr = (struct dirent *)malloc(directoryEntriesInBlock * sizeof(struct dirent));
	
	//Initializing the array with 0
	memset(arr, '\0', directoryEntriesInBlock * sizeof(struct dirent));

	//Make all the entries with validity zero
	for (int i = 0; i < directoryEntriesInBlock; i++){

		arr[i].valid = 0;

	}

	//Allocate a buffer in memory and then write it into the disk after filling it
	void * buffer = malloc(BLOCK_SIZE);
	memset(buffer, '\0', BLOCK_SIZE);

	memcpy(buffer, (void *)arr, directoryEntriesInBlock * sizeof(struct dirent));
	bio_write(blockNumber, buffer);

	free(arr);

}



/* 
 * Get available inode number from bitmap
 */
int get_avail_ino() {

	// Step 1: Read inode bitmap from disk
	
	// Step 2: Traverse inode bitmap to find an available slot

	// Step 3: Update inode bitmap and write to disk 

	//Traversing through bitmap
	for(int i = 0; i < MAX_INUM; i++){

		uint8_t bit = get_bitmap(inodeBitmap, i);

		if (bit == 0){
			//set this bit
			set_bitmap(inodeBitmap, i);

			//write the updated bitmap to disk
			bitmapToDisk('i');

			//return index for the bit
			return i;
		}

	}

	
	//Return as no inode is found
	return -1;

	
}

/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {

	// Step 1: Read data block bitmap from disk
	
	// Step 2: Traverse data block bitmap to find an available slot

	// Step 3: Update data block bitmap and write to disk 

	totalnumberOfBlks++;
	printf("Number of blocks used %d \n",totalnumberOfBlks);

	for(int i = 0; i < MAX_DNUM; i++){

		uint8_t bit = get_bitmap(dataBitmap, i);

		if (bit == 0){
			//set this bit
			set_bitmap(dataBitmap, i);

			//write the updated bitmap to disk
			bitmapToDisk('d');

			//return index
			return i;
		}

	}

	//Return as no data block is found
	
	return -1;

}

/* 
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode) {


	// Step 1: Get the inode's on-disk block number

	int blkNumber = ino/inodesInBlock;

	//Going to the particular block in inode region	
	int fetchBlockNumber = superBlock.i_start_blk + blkNumber;


  // Step 2: Get offset of the inode in the inode on-disk block

	
	int offset = (ino % inodesInBlock) ;


  // Step 3: Read the block from disk and then copy into inode structure

	void * fetchedBlock = malloc(BLOCK_SIZE);

	bio_read(fetchBlockNumber, fetchedBlock);

	struct inode * ptr;

	ptr = fetchedBlock;
	//Adding offset to reach the location of block number
	ptr = ptr + offset;

	//Copying the inode structure of that block number to the passed inode 
	memcpy((void *)inode, (const void *) ptr, sizeof(struct inode));


	free(fetchedBlock);
	return 0;
}

int writei(uint16_t ino, struct inode *inode) {

	// Step 1: Get the block number where this inode resides on disk
	
	
	int blkNumber = ino/inodesInBlock;

	//Going to the particular block in inode region	
	int fetchBlockNumber = superBlock.i_start_blk + blkNumber ;
	
	// Step 2: Get the offset in the block where this inode resides on disk

	int offset = (ino % inodesInBlock);


	// Step 3: Write inode to disk 

	void * fetchedBlock = malloc(BLOCK_SIZE);

	bio_read(fetchBlockNumber, fetchedBlock);

	struct inode * ptr;

	ptr = fetchedBlock;
	//Adding offset to reach the location of block number
	ptr = ptr + offset;

	//Copying the inode structure of that block number to the location pointing to block number
	memcpy((void *)ptr, (const void *) inode, sizeof(struct inode));

	//Writing the inode structure in the fetched block to the disk
	bio_write(fetchBlockNumber, fetchedBlock);


	free(fetchedBlock);
	return 0;
}



//Checking if the file exists in directory
int fileExists(struct inode presentDirectory, const char * fname){

	//Initializing found as 1 assuming file does not exist
	int found = 1;

	int totalDataBlocks = presentDirectory.size;

	
	for(int i = 0; i < totalDataBlocks; i++){
		//Getting the block pointed by direct pointer
		int dataBlock = presentDirectory.direct_ptr[i];

		
		//Allocating memory of block size and reading from disk to the buffer
		void * buffer = malloc(BLOCK_SIZE);
		bio_read(dataBlock, buffer);

		
		struct dirent * entry = buffer; 

		
		for (int j = 0; j < directoryEntriesInBlock; j++){

			//Checking the validity of file
			if((entry+j) -> valid == 1){

				char * fileName = (entry+j) -> name;

				//string comparison to check if it is the same file
				if (strcmp(fileName, fname) == 0){
					found = 0;
					j = directoryEntriesInBlock+1;
					i = totalDataBlocks + 1;
				}
			}
		}

		free(buffer);

	}

	return found;

}



/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {

  // Step 1: Call readi() to get the inode using ino (inode number of current directory)


    struct inode presentDir;
	//Get the inode structure of inode number passed to the current directory inode
	readi(ino, &presentDir);


  // Step 2: Get data block of current directory from inode
 
	//present directory has all inode information of direcotry

	int totalDatablocks = presentDir.size;


	
	for(int i = 0; i < totalDatablocks; i++){
		
		//Computing the data block by the direct pointer in inode structure
		int dataBlock = presentDir.direct_ptr[i];

		// Step 3: Read directory's data block and check each directory entry.
		//Reading the data block from disk to buffer
		void * buffer = malloc(BLOCK_SIZE);
		bio_read(dataBlock, buffer);

		struct dirent * temp = buffer; 

  		//If the name matches, then copy directory entry to dirent structure

		
		for (int j = 0; j < directoryEntriesInBlock; j++){

			//Checking the validity of directory entry
			if((temp+j) -> valid == 1){

				char * currentName = (temp+j) -> name;

				//compare string to check if directory entry matches with fname
				if (strcmp(currentName, fname) == 0){
					//we found the file
					//copying the dirent to the variable dirent
					//on finding file, copying the structure of that directory entry to dirent
					memcpy((void *) dirent, (void *)(temp + j), sizeof(struct dirent));
					free(buffer);
					return 1;
				}
			}
		}

		free(buffer);

	}

	return 0;
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {



	// Step 1: Read dir_inode's data block and check each directory entry of dir_inode

	int found = fileExists(dir_inode, fname);

	// Step 2: Check if fname (directory name) is already used in other entries
	if(found == 0){
		return -1;
	}

	// Step 3: Add directory entry in dir_inode's data block and write to disk

	// Allocate a new data block for this directory if it does not exist

	// Update directory inode

	// Write directory entry

	int checkDirEntry = 0;

	int totalDataBlocks = dir_inode.size;


	//loop through the datablocks
	for(int i = 0; i < totalDataBlocks; i++){

		//Computing the data block by the direct pointer in inode structure
		int dataBlock = dir_inode.direct_ptr[i];

		//Reading the data block from disk to buffer
		void * buffer = malloc(BLOCK_SIZE);
		bio_read(dataBlock, buffer);

		struct dirent * ptr = buffer; 

		//Checking the directory entries of this block
		for (int j = 0; j < directoryEntriesInBlock; j++){

			//Checking to find an empty entry
			if((ptr+j) -> valid == 0){

				checkDirEntry = 1;

				//Update directory entry at this location
				struct dirent * loc = ptr+j;
				loc -> ino = f_ino;
				loc -> valid = 1;
				//location -> name = fname;
				strcpy(loc->name, fname);
				loc -> len = name_len;

				//write this buffer back to the disk
				bio_write(dataBlock, buffer);

				j = directoryEntriesInBlock+1;
				i = totalDataBlocks + 1;

			}
		}

		free(buffer);

	}

	if(checkDirEntry == 0){

		//Get new block as no empty entry was found
		int newBLockNumber = get_avail_blkno() + superBlock.d_start_blk;

		//covert this block into an array of directory entries
		conversionToDirEntries (newBLockNumber);

		//update the direct_ptr & size of this directory's inode


		//Allocating memory for inode structure 
		struct inode * directoryInode = (struct inode *)malloc(sizeof(struct inode)); 

		readi(dir_inode.ino, directoryInode);
		//Updating the size and direct_ptr of inode of this directory
		directoryInode -> size += 1;

		*((directoryInode -> direct_ptr) + totalDataBlocks) = newBLockNumber;

		//Writing the updated inode to disk
		writei(dir_inode.ino, directoryInode);

		//Allocating memory of block size
		void * blk = malloc(BLOCK_SIZE);

		//Reading the new block to memory
		bio_read(newBLockNumber, blk);

		//Update directory entry at this location by filling information
		struct dirent * loc = blk;
		loc -> ino = f_ino;
		loc -> valid = 1;
		strcpy(loc->name, fname);
		loc -> len = name_len;

		//Writing this block to disk
		bio_write(newBLockNumber, blk);

		free(blk);

		free(directoryInode);


	}

	return 0;
}

int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and checks each directory entry of dir_inode
	

	int totalDatablocks = dir_inode.size;

	for(int i = 0; i < totalDatablocks; i++){

		//Computing the data block by the direct pointer in inode structure
		int dataBlock = dir_inode.direct_ptr[i];

		
		//Reading the data block from disk to buffer
		void * buffer = malloc(BLOCK_SIZE);
		bio_read(dataBlock, buffer);

		struct dirent * temp = buffer; 

		//loop through the dirents of this block
		for (int j = 0; j < directoryEntriesInBlock; j++){

			//Checking the validity of directory entry
			if((temp+j) -> valid == 1){

				char * currentName = (temp+j) -> name;

				// Step 2: Check if fname exist
	
				if (strcmp(currentName, fname) == 0){

					// Step 3: If exist, then remove it from dir_inode's data block and write to disk
					(temp + j) -> valid = 0;
					
					bio_write(dataBlock, buffer);

					free(buffer);
					return 1;
				}
			}
		}

		free(buffer);

	}

	return 0;
}

/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {
	
	// Step 1: Resolve the path name, walk through path, and finally, find its inode.
	// Note: You could either implement it in a iterative way or recursive way

	//Implementing the recursive way

	//Obtaining 2 duplicate paths to get base file and parent directory
	char * base = strdup(path);
	char * dir = strdup(path);
	

	char * baseFile = basename(base);
	char * parentDir = dirname(dir);
	

	if(strcmp(baseFile,"/")==0){
		//We have reached the root
		readi(0, inode);
        return 0;
	}

	//Allocating memory of directory entry structure size
	struct dirent * directoryEntry = (struct dirent *)malloc(sizeof(struct dirent));

	//Allocating memory of inode structure size
	struct inode * Inode = (struct inode *)malloc(sizeof(struct inode));

	//Calling it until we reach the root
    int temp =  get_node_by_path(parentDir, 0, Inode);

	if (temp == -1){
		//the path is invalid
		free(directoryEntry);
		free(Inode);
		return -1;

	}
	//Finding if the base directory/file is present in parent directory
	int temp1 = dir_find(Inode -> ino, baseFile, strlen(baseFile), directoryEntry);

    free(Inode);
	//Return if not present as path is not valid
	if (temp1 == 0){
	       free(directoryEntry);
            return -1;

        }
	//Getting the inode number of directory
	int inodeNum = directoryEntry->ino;
	//Read the inode structure using inode number which will be useful to check file in that directory
    readi(inodeNum, inode);

    free(directoryEntry);

	return 0;
}

/* 
 * Make file system
 */
int rufs_mkfs() {


	// Call dev_init() to initialize (Create) Diskfile
	dev_init(diskfile_path);

	// write superblock information
	
	superBlock.magic_num = MAGIC_NUM;
	superBlock.max_inum = MAX_INUM;
	superBlock.max_dnum = MAX_DNUM;

	
	//Compute the starting block of inode bitmap in order to fill it
	superBlock.i_bitmap_blk = sizeof(struct superblock)/BLOCK_SIZE;

	if (sizeof(struct superblock)%BLOCK_SIZE != 0){
		superBlock.i_bitmap_blk ++;
	}

	//Calculation for number of blocks required by inode bitmap

	int inodeBitmapBlocks = ((MAX_INUM)/8)/BLOCK_SIZE;

	if ((((MAX_INUM)/8)%BLOCK_SIZE) !=0){

			inodeBitmapBlocks++;

	}
	//Getting the starting block of data bitmap 
	superBlock.d_bitmap_blk = superBlock.i_bitmap_blk + inodeBitmapBlocks;

	int dataBitmapBlocks = ((MAX_DNUM)/8)/BLOCK_SIZE;

	if ((((MAX_DNUM)/8)%BLOCK_SIZE) !=0){

			dataBitmapBlocks++;

	}
	//Calculating starting block of inode region
	superBlock.i_start_blk = superBlock.d_bitmap_blk + dataBitmapBlocks;

	//Calculation for number of blocks required by inode region

	int inodeRegionBlocks = (sizeof(struct inode) * MAX_INUM)/BLOCK_SIZE;

	if ((sizeof(struct inode) * MAX_INUM)%BLOCK_SIZE !=0){

			inodeRegionBlocks++;

	}
	//Calculating starting block of data region
	superBlock.d_start_blk = superBlock.i_start_blk + inodeRegionBlocks;

	//ptrToSuperblock = &superBlock;

	memcpy((void *)ptrToSuperblock, (const void *) &superBlock, sizeof(struct superblock));


	bio_write(0, (const void *) ptrToSuperblock);


	// initialize inode bitmap

	
	for(int i = 0; i < (MAX_INUM/8); i ++){
        	inodeBitmap[i] = '\0';
	}

    bitmapToDisk('i');


	// initialize data block bitmap

	for(int i = 0; i < (MAX_DNUM/8); i ++){
		dataBitmap[i] = '\0';
	}

	bitmapToDisk('d');
	


	// updating the inode bitmap and data bitmap for root directory

	//giving first inode number to the root directory 
	set_bitmap(inodeBitmap, 0);
	bitmapToDisk('i');

	//setting first data block for the root directory
	set_bitmap(dataBitmap, 0);
	bitmapToDisk('d');

	//Formatting the data block to an array of directory entries
	conversionToDirEntries (superBlock.d_start_blk);

	// update inode for root directory
	struct inode rd;

	rd.ino = 0;
	rd.valid = 1;
	rd.size = 1;
	rd.type = D;
	rd.link = 2;
	rd.direct_ptr[0] = superBlock.d_start_blk;
	(rd.vstat).st_atime = time(NULL);
	(rd.vstat).st_mtime = time(NULL);
	(rd.vstat).st_mode = S_IFDIR | 0755;

	//Reading the starting block of inode region into buffer(memory)
	void * buffer = malloc(BLOCK_SIZE);
	bio_read(superBlock.i_start_blk, buffer);
	
	//Copying the updated inode structure to buffer
	memcpy(buffer, (const void *) &rd, sizeof(struct inode));
	//Writing the buffer of block size back to disk
	bio_write(superBlock.i_start_blk, (const void *)buffer);
	free(buffer);

	//adding the directory entry for current directory
	const char * filename = ".";
	dir_add(rd, 0, filename, 1);

	return 0;
}


/* 
 * FUSE file operations
 */
static void *rufs_init(struct fuse_conn_info *conn) {

	// Step 1a: If disk file is not found, call mkfs

  // Step 1b: If disk file is found, just initialize in-memory data structures
  // and read superblock from disk


  	pthread_mutex_lock(&mutex);
	// Step 1a: If disk file is not found, call mkfs
	if(dev_open(diskfile_path) == -1){

		ptrToSuperblock = malloc(BLOCK_SIZE);
		inodeBitmap = (bitmap_t) malloc((MAX_INUM)/8);
		dataBitmap = (bitmap_t) malloc((MAX_DNUM)/8);
		rufs_mkfs();

	}else{

  // Step 1b: If disk file is found, just initialize in-memory data structures
  // and read superblock from disk
  
	//Initializing in-memory data structures
	ptrToSuperblock = malloc(BLOCK_SIZE);
	inodeBitmap = (bitmap_t) malloc((MAX_INUM)/8);
	dataBitmap = (bitmap_t) malloc((MAX_DNUM)/8);

	bio_read(0, ptrToSuperblock);

	//superBlock = *ptrToSuperblock
	struct superblock * ptr = ptrToSuperblock;

	superBlock = *ptr;

	//read the inode bitmap from the disk to buffer
	void * buffer = malloc(BLOCK_SIZE);

	bio_read(superBlock.i_bitmap_blk,buffer);

	memcpy((void *)inodeBitmap, buffer, MAX_INUM/8);

	free(buffer);

	//read the data bitmap from the disk to buffer
	
	buffer = malloc(BLOCK_SIZE);

        bio_read(superBlock.d_bitmap_blk,buffer);

        memcpy((void *)dataBitmap, buffer, MAX_DNUM/8);

        free(buffer);

	}

	pthread_mutex_unlock(&mutex);
	return NULL;

}

static void rufs_destroy(void *userdata) {

	// Step 1: De-allocate in-memory data structures
	pthread_mutex_lock(&mutex);


	free(ptrToSuperblock);
	free(inodeBitmap);
	free(dataBitmap);

	// Step 2: Close diskfile

	dev_close();
	pthread_mutex_unlock(&mutex);
	

}

static int rufs_getattr(const char *path, struct stat *stbuf) {

	// Step 1: call get_node_by_path() to get inode from path

	pthread_mutex_lock(&mutex);
	
	//Allocating memory of inode structure 
	struct inode * inodeStr = (struct inode *)malloc(sizeof(struct inode));

	//Getting inode from path
	int valid = get_node_by_path(path, 0, inodeStr);

	//Invaild path
	if (valid == -1){
		free(inodeStr);
		pthread_mutex_unlock(&mutex);
		return -ENOENT;
	}


	// Step 2: fill attribute of file into stbuf from inode
	//Check both for file and directory
	if(inodeStr -> type == F){
		stbuf->st_mode   = S_IFREG | 0755;
	}else{
		stbuf->st_mode   = S_IFDIR | 0755;
	}
	stbuf->st_nlink  = 2;
	time(&stbuf->st_mtime);

	stbuf -> st_uid = getuid();
	stbuf -> st_gid = getgid();
	stbuf -> st_size = (inodeStr -> size) * BLOCK_SIZE;

	free(inodeStr);

	pthread_mutex_unlock(&mutex);
	return 0;

}

static int rufs_opendir(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path

	pthread_mutex_lock(&mutex);

	struct inode * inodeStr = (struct inode *)malloc(sizeof(struct inode));

	int valid = get_node_by_path(path, 0, inodeStr);

	// Step 2: If not find, return -1

	//Invaild path
	if (valid == -1){
		free(inodeStr);
		pthread_mutex_unlock(&mutex);
		return -1;
	}
	free(inodeStr);
    pthread_mutex_unlock(&mutex);
    return 0;




}

static int rufs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path

	pthread_mutex_lock(&mutex);	

	struct inode * inodeStr = (struct inode *)malloc(sizeof(struct inode));

	int check = get_node_by_path(path, 0, inodeStr);

	//Invaild path
	if (check == -1){
		free(inodeStr);
		pthread_mutex_unlock(&mutex);
		return -1;
	}


	// Step 2: Read directory entries from its data blocks, and copy them to filler

	//directory entries of inodeStr are read from its data blocks
	int totalDataBlocks = inodeStr -> size;

	//Traversing through the datablocks
	for(int i = 0; i < totalDataBlocks; i++){
		//Getting the data block by direct pointer pointing to it 
		int dataBlock = *((inodeStr -> direct_ptr) + i);

		//Read the buffer from disk in order to traverse through directory entries of the block
		void * blk = malloc(BLOCK_SIZE);
		bio_read(dataBlock, blk);

		struct dirent * temp = blk;

		//Checking the directory entries of block
		for (int j = 0; j < directoryEntriesInBlock; j++){

			//If valid copy them to filler
			if((temp+j) -> valid == 1){

			char * presentName = (temp+j) -> name;

			filler(buffer, presentName, NULL, 0);

			}
		}

		free(blk);

	}

	free(inodeStr);

	pthread_mutex_unlock(&mutex);
	return 0;
}


static int rufs_mkdir(const char *path, mode_t mode) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name

	pthread_mutex_lock(&mutex);

	char * dir = strdup(path);
	char * base = strdup(path);
	char * parentDir = dirname(dir) ;
	char * baseFile = basename(base);
	
	// Step 2: Call get_node_by_path() to get inode of parent directory

	struct inode * inodeStr = (struct inode *)malloc(sizeof(struct inode));

	int valid = get_node_by_path(parentDir, 0, inodeStr);

	//if path is not valid 
	if (valid == -1){
		pthread_mutex_unlock(&mutex);
		return 0;
	}


	// Step 3: Call get_avail_ino() to get an available inode number

	int check = get_avail_ino();
	//No inode number(space) available for file
	if(check == -1){
		pthread_mutex_unlock(&mutex);
		return 0;
	}


	// Step 4: Call dir_add() to add directory entry of target directory to parent directory
	

	int found = dir_add(*inodeStr, check, baseFile, strlen(baseFile));
	//If file already exists
	if(found == -1){
		unset_bitmap(inodeBitmap, check);
		bitmapToDisk('i');
		pthread_mutex_unlock(&mutex);
		return -1;
	}


	// Step 5: Update inode for target directory
	// Step 6: Call writei() to write inode to disk

	//updating parent directory's inode structure and then writing back to disk
	readi(inodeStr->ino, inodeStr);
	inodeStr -> link += 1;
	writei(inodeStr -> ino, inodeStr);

	//creating a new inode structure for the base directory

	struct inode * Inode = (struct inode *)malloc(sizeof(struct inode));

	Inode -> ino = check;
	Inode -> valid = 1;
	Inode -> size = 0;
	Inode -> type = D;
	// . and .. are the 2 default links
	Inode -> link = 2;
	time(&(Inode -> vstat).st_mtime);
	(Inode -> vstat).st_mode = RW;
	writei(check, Inode);

	//creating 2 directory entries '.' and '..' for the base file
	dir_add(*Inode, check, (const char *) ".", 1);
	readi(Inode->ino, Inode);
	dir_add(*Inode, inodeStr -> ino, (const char *)"..", 2);

	free(inodeStr);
	free(Inode);
	pthread_mutex_unlock(&mutex);
	return 0;
	

	
}

static int rufs_rmdir(const char *path) {


	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name

	pthread_mutex_lock(&mutex);

	char * dir = strdup(path);
	char * base = strdup(path);
	
	char * parentDir = dirname(dir);
    char * baseFile = basename(base);
	

	// Step 2: Call get_node_by_path() to get inode of target directory
	struct inode * inodeStr = (struct inode *)malloc(sizeof(struct inode));
	int check = get_node_by_path((char *) path, 0, inodeStr);
	if (check == -1){
		//path is not valid
		pthread_mutex_unlock(&mutex);
		return -1;
	}



		// Step 3: Clear data block bitmap of target directory
        
		//Formatting all the data blocks pointed by target and free those blocks in data bitmap
        int * temp = inodeStr -> direct_ptr;

        for(int i = 0; i < inodeStr -> size; i++){

                int * crrBlkNo = temp + i;

                int blockNumber = *crrBlkNo;

				//Formatting the particular block
				conversionToDirEntries(blockNumber);

                int index = blockNumber - superBlock.d_start_blk;
				
                unset_bitmap(dataBitmap, index);


        }

        // Step 4: Clear inode bitmap and its data block

        int inodeIndex = inodeStr -> ino;

        unset_bitmap(inodeBitmap, inodeIndex);

        //write these bitmaps to disk
        bitmapToDisk('i');
        bitmapToDisk('d');

	// Step 5: Call get_node_by_path() to get inode of parent directory
	struct inode * parentDirectoryInode = (struct inode *)malloc(sizeof(struct inode));
    get_node_by_path(parentDir, 0, parentDirectoryInode);

	// Step 6: Call dir_remove() to remove directory entry of target directory in its parent directory
	dir_remove(*parentDirectoryInode, baseFile, strlen(baseFile));

	free(parentDirectoryInode);
	free(inodeStr);
	pthread_mutex_unlock(&mutex);
	return 0;

	
}

static int rufs_releasedir(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {

	pthread_mutex_lock(&mutex);

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name
	char * dir = strdup(path);
	char * base = strdup(path);
	
	char * parentDir = dirname(dir);
	char * baseFile = basename(base);
	


	// Step 2: Call get_node_by_path() to get inode of parent directory

	//Allocating memory of inode structure
	struct inode * inodeStr = (struct inode *)malloc(sizeof(struct inode));

	int check = get_node_by_path(parentDir, 0, inodeStr);

	if (check == -1){
		//path is not valid
		pthread_mutex_unlock(&mutex);
		return 0;
	}


	// Step 3: Call get_avail_ino() to get an available inode number

	int iNumber = get_avail_ino();

	//No space available for file
	if(iNumber == -1){
		pthread_mutex_unlock(&mutex);
		return 0;
	}


	// Step 4: Call dir_add() to add directory entry of target file to parent directory

	int valid = dir_add(*inodeStr, iNumber, baseFile, strlen(baseFile));

	//Checking if file exists
	if(valid == -1){
		pthread_mutex_unlock(&mutex);
		return -1;
	}

	// Step 5: Update inode for target file
	struct inode * newTargetInode = (struct inode *)malloc(sizeof(struct inode));

	newTargetInode -> ino = iNumber;
	newTargetInode -> valid = 1;
	newTargetInode -> size = 0;
	newTargetInode -> type = F;
	newTargetInode -> link = 1;
	
	time(&(newTargetInode -> vstat).st_mtime);
	(newTargetInode -> vstat).st_mode = mode;
	
	// Step 6: Call writei() to write inode to disk
	writei(iNumber, newTargetInode);

	free(inodeStr);
	free(newTargetInode);
	
	pthread_mutex_unlock(&mutex);
	return 0;
}

	

static int rufs_open(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path

	pthread_mutex_lock(&mutex);

	struct inode * inodeStr = (struct inode *)malloc(sizeof(struct inode));

	int check = get_node_by_path(path, 0, inodeStr);

	// Step 2: If not find, return -1

	//Validity of path
	if (check == -1){
		free(inodeStr);
		pthread_mutex_unlock(&mutex);
		return -1;
	}
	free(inodeStr);
	pthread_mutex_unlock(&mutex);
	return 0;

}

static int rufs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {

	// Step 1: You could call get_node_by_path() to get inode from path

	// Step 2: Based on size and offset, read its data blocks from disk

	// Step 3: copy the correct amount of data from offset to buffer

	// Note: this function should return the amount of bytes you copied to buffer
	pthread_mutex_lock(&mutex);

	int sizeT = size;
	// Step 1: You could call get_node_by_path() to get inode from path
	struct inode * inodeStr = (struct inode *)malloc(sizeof(struct inode));

        int check = get_node_by_path(path, 0, inodeStr);

        if (check == -1){
                //path is not valid
                free(inodeStr);
		pthread_mutex_unlock(&mutex);
                return 0;
        }

	int sizeOfFile = (inodeStr -> size) * BLOCK_SIZE;
	//check
	if((offset > sizeOfFile) || (size > sizeOfFile)){
		pthread_mutex_unlock(&mutex);
		return 0;

	}

	// Step 2: Based on size and offset, read its data blocks from disk
	
	int blockIndex = offset/BLOCK_SIZE;
	int startBlock = *((inodeStr -> direct_ptr) + blockIndex);
	int startLocation = offset % BLOCK_SIZE;

	if ((sizeOfFile - offset < size)){
		pthread_mutex_unlock(&mutex);
		return 0;

	}

	// Step 3: copy the correct amount of data from offset to buffer
	char * newBuff = (char *)buffer;
	
	while(size > 0){

		int remaining = BLOCK_SIZE - startLocation;

		void * blk = malloc(BLOCK_SIZE);

		bio_read(startBlock, blk);

		char * temporaryBuff = (char *)blk;

		char * loc = temporaryBuff + startLocation;
		
		if (size <= remaining){
			memcpy((void *)newBuff, (void *)loc, size);
			free(blk);

			break;
		}

		memcpy((void *)newBuff, (void *)loc, remaining);
		newBuff += remaining;
		size = size - remaining;
		startLocation = 0;
		blockIndex ++;
		startBlock = *((inodeStr -> direct_ptr) + blockIndex); 
		free(blk);	
	}
	
	free(inodeStr);
	// Note: this function should return the amount of bytes you copied to buffer
	pthread_mutex_unlock(&mutex);
	return sizeT;
	
}

static int rufs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	// Step 1: You could call get_node_by_path() to get inode from path

	
	pthread_mutex_lock(&mutex);

	int sizeT = size;
	struct inode * inodeStr = (struct inode *)malloc(sizeof(struct inode));

        int check = get_node_by_path(path, 0, inodeStr);


        if (check == -1){
                //path is not valid
                free(inodeStr);
		pthread_mutex_unlock(&mutex);
                return 0;
        }


	
	

	// Step 2: Based on size and offset, read its data blocks from disk
	
	int blockIndex = offset/BLOCK_SIZE;
        int startLocation = offset % BLOCK_SIZE;


	//getting the extra data blocks needed for this operation
	int presentBlocks = inodeStr->size;

	//Total file size for this
	int totalsizeOfFile = (blockIndex * BLOCK_SIZE) + startLocation + size;
	//total number of blocks needed 
	int totalBlocks = totalsizeOfFile / BLOCK_SIZE;
	

	if (totalsizeOfFile % BLOCK_SIZE != 0){
		totalBlocks ++;
	}

	int diff = totalBlocks - presentBlocks;


	if (diff > 0){

		//we need to add extra blocks
		for (int index = 0; index < diff; index ++){

			int d = get_avail_blkno();
			if (d == -1){
				break;
			}
			*((inodeStr->direct_ptr) + presentBlocks + index) = d + superBlock.d_start_blk;
			inodeStr ->size++;
		}

	}

	if (inodeStr->size == 0){
		pthread_mutex_unlock(&mutex);
		return 0;
	}
	if ((inodeStr -> size) < blockIndex + 1){
		pthread_mutex_unlock(&mutex);
		return 0 ;
	}

	// Step 3: Write the correct amount of data from offset to disk

	int startBlock = *((inodeStr -> direct_ptr) + blockIndex);
	char * newBuff = (char *)buffer;

        while(size > 0){

                int remaining = BLOCK_SIZE - startLocation;

                void * blk = malloc(BLOCK_SIZE);

                bio_read(startBlock, blk);

                char * temporaryBuff = (char *)blk;

                char * loc = temporaryBuff + startLocation;

                if (size <= remaining){
                        memcpy((void *)loc, (void *)newBuff, size);
						bio_write(startBlock,blk);
                        free(blk);
                        break;
                }

                memcpy((void *)loc, (void *)newBuff, remaining);
		bio_write(startBlock, blk);
                newBuff += remaining;
                size = size - remaining;
                startLocation = 0;
                blockIndex ++;
		if((inodeStr -> size) < blockIndex + 1){
			time(&(inodeStr -> vstat).st_mtime);
			writei(inodeStr -> ino, inodeStr);
			pthread_mutex_unlock(&mutex);
			return sizeT - size;
				
		}
                startBlock = *((inodeStr -> direct_ptr) + blockIndex);
                free(blk);
        }

	// Step 4: Update the inode info and write it to disk
	time(&(inodeStr -> vstat).st_mtime);


	writei(inodeStr -> ino, inodeStr);
	// Note: this function should return the amount of bytes you write to disk
	
	free(inodeStr);
	pthread_mutex_unlock(&mutex);
	return sizeT;
	
}

static int rufs_unlink(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name



	pthread_mutex_lock(&mutex);

	char * dir = strdup(path);
	char * base = strdup(path);
	
	char * parentDir = dirname(dir);
	char * baseFile = basename(base);
	

	// Step 2: Call get_node_by_path() to get inode of target file
	struct inode * baseInode = (struct inode *)malloc(sizeof(struct inode));

    int valid = get_node_by_path(path, 0, baseInode);

    if (valid == -1){
            free(baseInode);
			pthread_mutex_unlock(&mutex);
            return 0;
     }

	// Step 3: Clear data block bitmap of target file
	for(int k = 0; k < baseInode -> size; k++){
		//Get the index of data block bitmap you want to clear
		int removeBlock = *((baseInode -> direct_ptr) + k);
		int dataBlockIndex = removeBlock - superBlock.d_start_blk;
		unset_bitmap(dataBitmap, dataBlockIndex);

	}


	// Step 4: Clear inode bitmap and its data block
	unset_bitmap(inodeBitmap, baseInode -> ino);
	bitmapToDisk('i');
	bitmapToDisk('d');

	baseInode -> valid = 0;
	writei(baseInode -> ino, baseInode);

	// Step 5: Call get_node_by_path() to get inode of parent directory
	struct inode * parentDirectoryInode = (struct inode *)malloc(sizeof(struct inode));
	get_node_by_path(parentDir, 0, parentDirectoryInode);
	
	// Step 6: Call dir_remove() to remove directory entry of target file in its parent directory
	dir_remove(*parentDirectoryInode, baseFile, strlen(baseFile));
	pthread_mutex_unlock(&mutex);
	return 0;

}

static int rufs_truncate(const char *path, off_t size) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_release(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int rufs_flush(const char * path, struct fuse_file_info * fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_utimens(const char *path, const struct timespec tv[2]) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}


static struct fuse_operations rufs_ope = {
	.init		= rufs_init,
	.destroy	= rufs_destroy,

	.getattr	= rufs_getattr,
	.readdir	= rufs_readdir,
	.opendir	= rufs_opendir,
	.releasedir	= rufs_releasedir,
	.mkdir		= rufs_mkdir,
	.rmdir		= rufs_rmdir,

	.create		= rufs_create,
	.open		= rufs_open,
	.read 		= rufs_read,
	.write		= rufs_write,
	.unlink		= rufs_unlink,

	.truncate   = rufs_truncate,
	.flush      = rufs_flush,
	.utimens    = rufs_utimens,
	.release	= rufs_release
};


int main(int argc, char *argv[]) {
	int fuse_stat;

	getcwd(diskfile_path, PATH_MAX);
	strcat(diskfile_path, "/DISKFILE");

	fuse_stat = fuse_main(argc, argv, &rufs_ope, NULL);

	return fuse_stat;
}



//This function writes bitmaps to the disk
void bitmapToDisk(char c){

	//Allocating the memory equal to size of block
	void * buffer = malloc(BLOCK_SIZE);
	//
	if(c == 'i'){
		//Copy the inode bitmap to buffer and the write it to disk
    	memcpy(buffer, (const void *) inodeBitmap, (MAX_INUM)/8);
    	bio_write(superBlock.i_bitmap_blk, (const void *)buffer);
	}else{
		//Copy the data bitmap to buffer and the write it to disk
		memcpy(buffer, (const void *) dataBitmap, (MAX_DNUM)/8);
		bio_write(superBlock.d_bitmap_blk, (const void *)buffer);
	}
    
	//Free the memory allocated as buffer
    free(buffer);

}


