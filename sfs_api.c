#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sfs_api.h"
#include "disk_emu.h"


//////////////////////////
// I-Node/J-Node Struct //
//////////////////////////
typedef struct {
	int size;
	int direct_ptr[14];
	int indirectPtr;	
} Node;

////////////////////////////
// Directory Entry Struct //
////////////////////////////
typedef struct {
	char filename[10];
	int inode_nb;
} Directory_entry;

//////////////////////////////////////
// Open File Descriptor Table Entry //
//////////////////////////////////////
typedef struct {
	int inode_nb;
	int read_ptr;
	int write_ptr;
} Fd_entry;

///////////////////////////////
// Global constant variables //
///////////////////////////////

// Disk Name
char* DISK_NAME = "260637833_ssfs";
// Block Size
const int SIZE_BLOCK = 1024;
// Number of Data Blocks
const int NUMBER_DATA_BLOCKS = 1024;
// Number of Data Blocks + Super Block, FBM, and VM
const int FILE_SYSTEM_SIZE = 1027;
// Starting address of Super Block
const int SB_STARTING_ADDRESS = 0;
// Starting address of Data Blocks
const int DB_STARTING_ADDRESS = 1;
// Starting address of Free Bit Map
const int FBM_STARTING_ADDRESS = 1025;

// Starting address of Write Mask
const int WM_STARTING_ADDRESS = 1026;

// Maximum i-nodes
const int MAX_FILES = 199;


/////////////////////
// Local variables //
/////////////////////

// Root Directory Cache
Directory_entry* root_dir_cache;
// FBM Cache
char* fbm_cache;
// Root JNode Cache
Node* root_jnode;

/* SHADOW
// WM Cache
*/

// Open File Descriptor Table
// SIZE MUST MATCH MAX_FILES
Fd_entry Open_Fd_Table[199];

/*
/ Initialize root j-node in cache
*/
Node* getRootJNode() {
	if (root_jnode == NULL) {
		// Get root j node from disk
		int* sb_int_ptr = (int*) malloc(SIZE_BLOCK);
		read_blocks(SB_STARTING_ADDRESS, 1, sb_int_ptr);

		// Store root j node in cache
		root_jnode = (Node*) malloc(sizeof(Node));
		memcpy(root_jnode, &(sb_int_ptr[4]), sizeof(Node));
		free(sb_int_ptr);
	}
	return root_jnode;
}

/*
/ Update Super block in memory whenever you make changes to root j-node
*/
void updateSB() {
	// Get root j-node
	int* sb_int_ptr = (int*) malloc(SIZE_BLOCK);
	read_blocks(SB_STARTING_ADDRESS, 1, sb_int_ptr);
	// Overwrite previous root jnode
	memcpy(&(sb_int_ptr[4]),root_jnode, sizeof(Node));
	write_blocks(SB_STARTING_ADDRESS, 1, sb_int_ptr);
	free(sb_int_ptr);	
}

/*
/ Initialize the directory cache
*/
void initialize_directory_cache() {	
	if (root_dir_cache == NULL) {
		root_dir_cache = (Directory_entry*) malloc(SIZE_BLOCK*4);
	}
	Node* initial_inode = (Node*) malloc(SIZE_BLOCK);

	// Look for root j-node
	if (root_jnode == NULL) {
		getRootJNode();
	}
	// Get 0th i-node to get root directory 
	read_blocks(DB_STARTING_ADDRESS + root_jnode[0].direct_ptr[0], 1, initial_inode);

	// Read root directory blocks into cache from the 0th inode first 4 pointers
	// Note: 0th i-node always points to root directory
	read_blocks(DB_STARTING_ADDRESS + initial_inode[0].direct_ptr[0], 1, &(root_dir_cache[0]));
	read_blocks(DB_STARTING_ADDRESS + initial_inode[0].direct_ptr[1], 1, &(root_dir_cache[SIZE_BLOCK/sizeof(Directory_entry)]));
	read_blocks(DB_STARTING_ADDRESS + initial_inode[0].direct_ptr[2], 1, &(root_dir_cache[2*SIZE_BLOCK/sizeof(Directory_entry)]));
	read_blocks(DB_STARTING_ADDRESS + initial_inode[0].direct_ptr[3], 1, &(root_dir_cache[3*SIZE_BLOCK/sizeof(Directory_entry)]));

	free(initial_inode);
}

/*
/ Initializes fbm cache by setting up local variable and filling it up
*/
void initialize_fbm_cache() {
	if (fbm_cache == NULL) {
		fbm_cache = (char*) malloc(SIZE_BLOCK);
	}
	read_blocks(FBM_STARTING_ADDRESS, 1, fbm_cache);
}

/*
/ Change value of a specific data block with a new value (1 = used, 0 = unused)
*/
int modify_fbm(int blocknb, int newValue) {
	if (fbm_cache == NULL) {
		initialize_fbm_cache();
	}
	// Check if they are different values. If they are the same, that means that the block was already used/unused
	if (fbm_cache[blocknb] == newValue) {
		printf("Error: Can't modify as the block is already occupied/empty\n");
		return -1;
	}
	else {
		fbm_cache[blocknb] = newValue;
		// Change fbm on disk
		write_blocks(FBM_STARTING_ADDRESS, 1, fbm_cache);		
		return 0;
	}
}

/*
/ Set a directory from scratch and save it in cache
*/ 
void set_directory() {
	Directory_entry* dir_buffer = (Directory_entry*) malloc(SIZE_BLOCK);
	// Empty directory entry
	Directory_entry dir_entry;
	strcpy(dir_entry.filename, "");
	dir_entry.inode_nb = -1;
	// Copy into buffer the empty directories
	for (int i=0; i<SIZE_BLOCK/sizeof(Directory_entry); i++) {
		memcpy(&(dir_buffer[i]), &dir_entry, sizeof(Directory_entry));
	}
	// Allocate into last data blocks, the root directory as shown in the picture of the assignment
	write_blocks(DB_STARTING_ADDRESS + FBM_STARTING_ADDRESS-4-1, 1, dir_buffer);
	write_blocks(DB_STARTING_ADDRESS + FBM_STARTING_ADDRESS-3-1, 1, dir_buffer);
	write_blocks(DB_STARTING_ADDRESS + FBM_STARTING_ADDRESS-2-1, 1, dir_buffer);
	// We can only have up to 199 files. Therefore, the rest of the entries are marked as unusable
	strcpy(dir_entry.filename, "UNUSABLE");
	dir_entry.inode_nb = 100000;
	// Copying unusable directory entries into memory
	for (int i=7; i<SIZE_BLOCK/sizeof(Directory_entry); i++) {
		memcpy(&(dir_buffer[i]), &dir_entry, sizeof(Directory_entry));
	}
	write_blocks(DB_STARTING_ADDRESS + FBM_STARTING_ADDRESS-1-1, 1, dir_buffer);
	free(dir_buffer);
}

/*
/ FBM set up. Initialize each memory except the first one and the last 4 ones to unused data blocks
*/
void set_fbm() {
	char* fbm_buffer = (char*) malloc(SIZE_BLOCK);
	for (int i = 0; i < SIZE_BLOCK; i++) {
		fbm_buffer[i] = 0;
	}

	// Need to modify FBM as the first data block is used for the i-node
	modify_fbm(0, 1);
	// Need to modify FBM as the last 4 data block is used for the root directory
	modify_fbm(SIZE_BLOCK-1, 1);
	modify_fbm(SIZE_BLOCK-2, 1);
	modify_fbm(SIZE_BLOCK-3, 1);
	modify_fbm(SIZE_BLOCK-4, 1);

	free(fbm_buffer);
}

/*
/ Update the root directory on the disk
*/
void update_directory_disk() {
	Node* initial_inode = (Node*) malloc(SIZE_BLOCK);
	// Look for root j-node
	if (root_jnode == NULL) {
		getRootJNode();
	}
	// Get 0th i-node to get root directory 
	read_blocks(DB_STARTING_ADDRESS + root_jnode[0].direct_ptr[0], 1, initial_inode);
	// Update directory on disk
	// Note: 0th i-node always points to root directory
	write_blocks(DB_STARTING_ADDRESS + initial_inode[0].direct_ptr[0], 1, &(root_dir_cache[0]));
	write_blocks(DB_STARTING_ADDRESS + initial_inode[0].direct_ptr[1], 1, &(root_dir_cache[SIZE_BLOCK/sizeof(Directory_entry)]));
	write_blocks(DB_STARTING_ADDRESS + initial_inode[0].direct_ptr[2], 1, &(root_dir_cache[2*SIZE_BLOCK/sizeof(Directory_entry)]));
	write_blocks(DB_STARTING_ADDRESS + initial_inode[0].direct_ptr[3], 1, &(root_dir_cache[3*SIZE_BLOCK/sizeof(Directory_entry)]));

	free(initial_inode);
}


/*
/ Set up the WM by intializing everything to 0. 
*/ 
void set_wm() {
	char* wm_buffer = (char*) malloc(SIZE_BLOCK);
	for (int i = 0; i < SIZE_BLOCK; i++) {
		wm_buffer[i] = 0;
	}		
	// First argument:WM is the 1027th block of the file system. So start address = 1026.
	// Second argument: WM is one single block
	// Third argument: Buffer that contains the WM information
	write_blocks(WM_STARTING_ADDRESS, 1, wm_buffer);
	free(wm_buffer);
}

/*
/ Find an empty block and allocate it by modifying fbm
*/
int find_empty_data_block() {
	// Iterate through FBM to find unallocated blocks for the file
	for (int j=0; j<NUMBER_DATA_BLOCKS; j++) {
		if (fbm_cache[j] == 0) {
			
			// Modify FBM in disk and cache
			modify_fbm(j, 1);
			
			return j;
		}
	}
	// No more blocks available
	printf("Error: No more available blocks\n");
	return -1;
}

/*
/ Find an empty file descriptor and return its index
*/
int find_empty_fd() {
	for (int i=0; i<MAX_FILES; i++) {
		if (Open_Fd_Table[i].inode_nb == -1) {
			return i;
		}
	}
	// No more fd entries available
	printf("Error: No more available file descriptors\n");
	return -1;
}

/*
/ Initialize a new block of i-nodes in blocknb
*/
int initialize_new_inode_block(int blocknb) {

	// Update root J-Node size since we are initializing a new block of inodes
	if (root_jnode == NULL) {
		getRootJNode();
	}
	(*root_jnode).size += SIZE_BLOCK;
	updateSB();

	// Create a block with empty i-nodes
	Node* inode_buffer = (Node*) malloc(SIZE_BLOCK);

	//
	// Create empty i-nodes
	//
	Node empty_inode;
	empty_inode.size = -1;
	for (int i=0; i<14; i++) {
		empty_inode.direct_ptr[i] = -1;
	}
	empty_inode.indirectPtr = -1;	
	// Copy to empty i nodes to buffer	
	for (int i=0; i<SIZE_BLOCK/sizeof(Node); i++) {
		memcpy(&(inode_buffer[i]), &empty_inode, sizeof(Node));
	}
	write_blocks(DB_STARTING_ADDRESS + blocknb, 1, inode_buffer);

	free(inode_buffer);

	return 0;
}

/*
/ Add new root directory entry to root directory in cache and in disk
*/
int add_new_root_directory_entry(char* name, int inode_nb) {

	int entry_index = -1;
	// Create new directory entry
	Directory_entry entry;
	strcpy(entry.filename, name);
	entry.inode_nb = inode_nb;

	// Find empty entry in root_directory
	for (int i=0; i<MAX_FILES; i++) {
		if (strcmp(root_dir_cache[i].filename, "") == 0) {
			// Modify root_directory in cache
			memcpy(&(root_dir_cache[i]), &entry, sizeof(Directory_entry));
			entry_index = i;
			break;
		}
	}

	if (entry_index == -1) {
		printf("Error: Not enough space in root directory\n");
		return -1;
	}

	// Get root j node
	if (root_jnode == NULL) {
		getRootJNode();
	}
	// Modify root_directory in disk
	update_directory_disk();
	return 0;
}

/*
/ Return the file size based on the i-node nb
*/
int get_file_size(int inode_nb) {
	if (inode_nb == -1) {
		printf("Error: Negative inode_nb\n");
		return 0;
	}
	// Get root j node
	if (root_jnode == NULL) {
		getRootJNode();
	}
	int direct_ptr_nb = inode_nb/(SIZE_BLOCK/sizeof(Node));
	int block_nb = (*root_jnode).direct_ptr[direct_ptr_nb];
	int inode_index_in_block = inode_nb % (SIZE_BLOCK/sizeof(Node));

	if (block_nb > -1) {
		// Get i-node block
		Node* inode_block = (Node*) malloc(SIZE_BLOCK);
		read_blocks(DB_STARTING_ADDRESS + block_nb, 1, inode_block);
		// Get inode
		Node file_inode = inode_block[inode_index_in_block];
		// Return size
		int size = file_inode.size;
		if (file_inode.indirectPtr != -1)
			size += get_file_size(file_inode.indirectPtr);
		free (inode_block);
		return size;		
	}
	else {
		printf("Error: Negative Block number\n");
		return 0;
	}
}


/*
/ Update the size of a file based on the i-node nb to the value in the second argument
*/
int update_file_size(int inode_nb, int write_ptr, int length) {
	if (inode_nb == -1) {
		printf("Error: Negative inode_nb\n");
		return -1;
	}
	// Get root j node
	if (root_jnode == NULL) {
		getRootJNode();
	}
	int direct_ptr_nb = inode_nb/(SIZE_BLOCK/sizeof(Node));
	int block_nb = (*root_jnode).direct_ptr[direct_ptr_nb];
	int inode_index_in_block = inode_nb % (SIZE_BLOCK/sizeof(Node));

	if (block_nb > -1) {
		// Get corresponding i-node block
		Node* inode_block = (Node*) malloc(SIZE_BLOCK);
		read_blocks(DB_STARTING_ADDRESS + block_nb, 1, inode_block);

		int old_size = get_file_size(inode_nb);
		// Size changed
		if (old_size < write_ptr + length) {
			inode_block[inode_index_in_block].size = write_ptr + length;
			write_blocks(DB_STARTING_ADDRESS + block_nb, 1, inode_block);
		}
		free(inode_block);
		return write_ptr + length;
	}
	else {
		printf("Error: Negative Block number\n");
		return -1;
	}	
}

/*
/ Copy into the buffer, the file data block of an i-node based on the inode's direct pointer index (know which block of the file to get)
*/
int copy_data_block(int inode_nb, char* buffer, int inode_direct_ptr_index) {
	if (inode_nb == -1) {
		printf("Error: Negative inode_nb\n");
		return -1;
	}
	// Get root j node
	if (root_jnode == NULL) {
		getRootJNode();
	}
	int direct_ptr_nb = inode_nb/(SIZE_BLOCK/sizeof(Node));
	int block_nb = (*root_jnode).direct_ptr[direct_ptr_nb];
	int inode_nb_in_block = inode_nb % (SIZE_BLOCK/sizeof(Node));

	if (block_nb > -1) {
		// Get i-node block
		Node* inode_block = (Node*) malloc(SIZE_BLOCK);
		read_blocks(DB_STARTING_ADDRESS + block_nb, 1, inode_block);
		// Get i-node
		Node inode = inode_block[inode_nb_in_block];
		int file_block_nb = inode.direct_ptr[inode_direct_ptr_index];

		// Store into buffer, the latest block of the file
		read_blocks(DB_STARTING_ADDRESS + file_block_nb, 1, buffer);
		free(inode_block);
		// Return new data block number
		return file_block_nb;	
	}
	else {
		printf("Error: Negative Block number\n");
		return -1;
	}
}

/*
/ Function that returns the new data block allocated for a file and that allocates a new data block to a file by updating the i-node's direct pointers
*/
int get_data_block(int inode_nb) {
	// Can't get an inode with a negative number
	if (inode_nb == -1) {
		printf("Error: inode nb is in the wrong range\n");
		return -1;
	}
	// Get root j node
	if (root_jnode == NULL) {
		getRootJNode();
	}
	// Integer divison to find which inode block
	// Modulus operator to find which inode in the block of inodes
	int direct_ptr_nb = inode_nb/(SIZE_BLOCK/sizeof(Node));
	int block_nb = (*root_jnode).direct_ptr[direct_ptr_nb];
	int inode_nb_in_block = inode_nb % (SIZE_BLOCK/sizeof(Node));

	if (block_nb > -1) {
		// Allocate buffer for the i-node block
		Node* buffer = (Node*) malloc(SIZE_BLOCK);
		read_blocks(DB_STARTING_ADDRESS + block_nb, 1, buffer);

		int db_nb = find_empty_data_block();
		if (db_nb > -1) {
			// Get file's i-node and modify pointers
			Node inode = buffer[inode_nb_in_block];
			// boolean indicating if the block has been allocated to a direct pointer of the i-node
			int found = 0;
			for (int i=0; i<14; i++) {
				// Empty direct pointer
				if (inode.direct_ptr[i] == -1) {
					inode.direct_ptr[i] = db_nb;
					memcpy(&(buffer[inode_nb_in_block]), &inode, sizeof(Node));
					// Modify i-node block
					found = 1;
					write_blocks(DB_STARTING_ADDRESS + block_nb, 1, buffer);
					break;
				}
			}
			free(buffer);
			// Need to use indirect pointer to point to a new inode
			if (found == 0) {
				
				printf("Error: No data blocks or not enough direct pointers available\n");
				return -1;
			}
		}
		else {
			printf("Error: No data blocks available\n");
			free(buffer);
			return -1;
		}
		return block_nb;	
	}
	else {
		printf("Error: Negative Block number\n");
		return -1;
	}	
}


//
// Create/Load file system
//
void mkssfs(int fresh){
	// Initialize new fresh disk
	if (fresh == 1) {

		int init = init_fresh_disk(DISK_NAME, SIZE_BLOCK, FILE_SYSTEM_SIZE);
		// Check if error initializing disk
		if (init == -1) {
			printf("Error: init_fresh_disk returned -1\n");
		}

		////////////////////////
		// Set up Super Block //
		////////////////////////
		int sb_byte_count = 0;
		int* sb_buffer_start = (int*) malloc(SIZE_BLOCK);
		int* sb_buffer = sb_buffer_start;

		//
		// Initialize Magic Entry
		//
		*sb_buffer = 0xACBD0005;
		sb_buffer++;
		sb_byte_count += sizeof(int);
		//
		// Initialize Block Size
		//
		*sb_buffer = SIZE_BLOCK;
		sb_buffer++;
		sb_byte_count += sizeof(int);
		//
		// Initialize File System Size
		//
		*sb_buffer = FILE_SYSTEM_SIZE;
		sb_buffer++;
		sb_byte_count += sizeof(int);
		//
		// Initialize Number of i-nodes
		//
		// 1 i-node for directory, so 200 total
		*sb_buffer = MAX_FILES+1;
		sb_buffer++;
		sb_byte_count += sizeof(int);		

		// Cast pointer type
		Node* sb_buffer_node = (Node*) sb_buffer;

		//
		// Initialize Root (j-nodes)
		//
		Node root;
		// size is 1024 since it should contain at least one i-node block of 1024 bytes which contains an i-node that points to the root directory
		root.size = 1024;
		for (int i=0; i<14; i++) {
			root.direct_ptr[i] = -1;
		}
		// ptr at index 0 points to first i-node block which is located in the 0th data block
		root.direct_ptr[0] = 0;
		root.indirectPtr = -1;

		memcpy(sb_buffer_node, &root, sizeof(Node));
		sb_buffer_node++;
		sb_byte_count += sizeof(Node);

		//
		// Initialize Empty Shadow Roots
		//
		while (sb_byte_count+sizeof(Node) < SIZE_BLOCK) {
			Node shadow_root;
			shadow_root.size = -1;
			for (int i=0; i<14; i++) {
				shadow_root.direct_ptr[i] = -1;
			}
			shadow_root.indirectPtr = -1;

			memcpy(sb_buffer_node, &shadow_root, sizeof(Node));
			sb_buffer_node++;
			sb_byte_count += sizeof(Node);
		}

		//
		// Initialy rest of memory with 0s
		//
		sb_buffer = (int*) sb_buffer_node;
		int empty_space = 0;
		while (sb_byte_count < SIZE_BLOCK) {
			memcpy(sb_buffer, &empty_space, sizeof(int));
			sb_buffer++;
			sb_byte_count += sizeof(int);
		}	

		// First argument: Super Block is the first block of the file system. So start address = 0.
		// Second argument: Super Block is one single block
		// Third argument: Buffer that contains the super block information
		write_blocks(SB_STARTING_ADDRESS, 1, sb_buffer_start);

		////////////////
		// Set up FBM //
		////////////////
		set_fbm();

		////////////////
		// Set up WM //
		////////////////
		set_wm();

		////////////////////////////////////////
		// Initialize file containing i-nodes //
		////////////////////////////////////////
		
		// Create a block with one i-node and filled with other empty i-nodes
		Node* inode_buffer = malloc(SIZE_BLOCK);

		//
		// Create directory i-node
		//
		Node directory_inode;

		// i-node of size 4 blocks (max 199 files, which span 3.1 blocks, the rest are empty)
		directory_inode.size = SIZE_BLOCK*4;
		// ptr to the last data blocks for the directory
		// ROOT DIRECTORY IN THE LAST 4 DATA BLOCK
		directory_inode.direct_ptr[0] = FBM_STARTING_ADDRESS-4-1;
		directory_inode.direct_ptr[1] = FBM_STARTING_ADDRESS-3-1;
		directory_inode.direct_ptr[2] = FBM_STARTING_ADDRESS-2-1;
		directory_inode.direct_ptr[3] = FBM_STARTING_ADDRESS-1-1;
		for (int i=4; i<14; i++) {
			directory_inode.direct_ptr[i] = -1;
		}
		directory_inode.indirectPtr = -1;
		memcpy(inode_buffer, &directory_inode, sizeof(Node));
		//
		// Create empty i-nodes
		//
		Node empty_inode;
		empty_inode.size = -1;
		for (int i=0; i<14; i++) {
			empty_inode.direct_ptr[i] = -1;
		}
		empty_inode.indirectPtr = -1;		
		for (int i=1; i<SIZE_BLOCK/sizeof(Node); i++) {
			memcpy(&(inode_buffer[i]), &empty_inode, sizeof(Node));
		}
		write_blocks(DB_STARTING_ADDRESS, 1, inode_buffer);


		///////////////////////////
		// Set up Root Directory //
		///////////////////////////
		set_directory();

		//////////////////////
		// Free All Buffers //
		//////////////////////
		free(sb_buffer_start);
		free(inode_buffer);

	}
	// Initialize existing disk
	else {
		int init = init_disk(DISK_NAME, SIZE_BLOCK, FILE_SYSTEM_SIZE);
		if (init == -1) {
			printf("Error: init_disk returned -1\n");
		}
	}

	///////////////////
	// Set up Caches //
	///////////////////

	// Root Directory Cache
	initialize_directory_cache();

	// FBM Cache
	initialize_fbm_cache();

	// Set up root j-node
	getRootJNode();

	// Open File Descriptor Table
	for (int i=0; i<MAX_FILES; i++) {
		Open_Fd_Table[i].inode_nb = -1;
		Open_Fd_Table[i].read_ptr = -1;
		Open_Fd_Table[i].write_ptr = -1;
	}
}

/*
// Open the given file with the given name and return the file's ID
*/
int ssfs_fopen(char *name){

	// CHECK FILE EXISTS
	int file_inode_nb = -1;
	// Iterate through root directory and find matching filename
	for (int i=0; i<MAX_FILES; i++) {
		if (strcmp(name, root_dir_cache[i].filename) == 0) {
			file_inode_nb = root_dir_cache[i].inode_nb;
			break;
		}
	}

	////////////////////////////////////
	// SCENARIO 1: FILE DOESN'T EXIST //
	////////////////////////////////////
	if (file_inode_nb == -1) {

		////////////////////////////////////
		// Initialize and Allocate i-node //
		////////////////////////////////////

		// get list of i-node blocks from j-node superblock
		if (root_jnode == NULL) {
			getRootJNode();
		}

		// NESTED FOR LOOP: 1st Loop: Scan through i-node file pointed by the root
		//                  2nd Loop: Scan through each i-node block and find an empty i-node with size -1
		// iterate through each inode block
		for (int i=0; i<14; i++) {
			// Hold block of inodes
			Node* block_inode = (Node*) malloc(SIZE_BLOCK);
			// Unintialized block of i-nodes
			if ((*root_jnode).direct_ptr[i] == -1) {
				int inode_block_nb = find_empty_data_block();
				if (inode_block_nb == -1) {
					free(block_inode);
					return -1;
				}
				initialize_new_inode_block(inode_block_nb);
				// Add new inode block to jroot
				(*root_jnode).direct_ptr[i] = inode_block_nb;
				// update j root in SB
				updateSB();
			}
			read_blocks(DB_STARTING_ADDRESS + (*root_jnode).direct_ptr[i], 1, block_inode);

			// iterate through each individual inode in a block
			for (int x=0; x<SIZE_BLOCK/sizeof(Node); x++) {
				// found empty inode
				if (block_inode[x].size == -1) {
					// Change size to 0 to occupy it
					block_inode[x].size = 0;
					// Update i-node block in disk
					write_blocks(DB_STARTING_ADDRESS + (*root_jnode).direct_ptr[i], 1, block_inode);
					free(block_inode);

					int inode_nb = i*SIZE_BLOCK/sizeof(Node) + x;
					///////////////////////////////////
					// Allocate root_directory entry //
					///////////////////////////////////
					add_new_root_directory_entry(name, inode_nb);

					////////////////////////////////////
					// Allocate open file table entry //
					////////////////////////////////////
					int fd_index = find_empty_fd();
					if (fd_index > -1 && fd_index < MAX_FILES) {
						Open_Fd_Table[fd_index].inode_nb = inode_nb;
						Open_Fd_Table[fd_index].read_ptr = 0;
						Open_Fd_Table[fd_index].write_ptr = 0;
						// return new file descriptor index
						return fd_index;						
					}
					printf("Error: Not enough space in open file table entry\n");
					return -1;
				}
			}
			free(block_inode);
		}	
	}
	/////////////////////////////
	// SCENARIO 2: FILE EXISTS //
	/////////////////////////////
	else {
		// Check if it already exists in the open file table
		for (int i=0; i<MAX_FILES; i++) {
			if (Open_Fd_Table[i].inode_nb == file_inode_nb) {
				return i;
			}
		}
		// File is not in the open file table so open in append mode
		int fd_index = find_empty_fd();
		if (fd_index > -1 && fd_index < MAX_FILES) {
			// Get root j node
			if (root_jnode == NULL) {
				getRootJNode();
			}
			int direct_ptr_nb = file_inode_nb/(SIZE_BLOCK/sizeof(Node));
			int block_number = (*root_jnode).direct_ptr[direct_ptr_nb];
			int inode_nb_in_block = file_inode_nb % (SIZE_BLOCK/sizeof(Node));
			if (block_number > -1) {
				// Get inode block
				Node* inode_block = (Node*) malloc(SIZE_BLOCK);
				read_blocks(DB_STARTING_ADDRESS + block_number, 1, inode_block);
				// Get inode
				Node file_inode = inode_block[inode_nb_in_block];
				// Change file descriptor entry
				Fd_entry entry;
				entry.inode_nb = file_inode_nb;
				entry.read_ptr = 0;
				entry.write_ptr = file_inode.size;
				// Copy to cache
				memcpy(&(Open_Fd_Table[fd_index]), &entry, sizeof(Fd_entry));
				free(inode_block);
				return fd_index;
			}
			else {
				printf("Error: block_number is negative\n");
				return -1;				
			}

			// return new file descriptor index
			return fd_index;						
		}
		else {
			printf("Error: Not enough space in open file table entry\n");
			return -1;			
		}
	}
    return 0;
}


/*
/ Close the given file based on the file's ID
*/
int ssfs_fclose(int fileID){

	if (fileID < 0 || fileID > MAX_FILES) {
		printf("Error: Incorrect fileID\n");
		return -1;
	}
	// Check if previously occupied
	if (Open_Fd_Table[fileID].inode_nb == -1)
	{
		printf("Error: File Descriptor Entry was initially empty\n");
		return -1;
	}
	else {
		// Create an empty fd
		Open_Fd_Table[fileID].inode_nb = -1;
		Open_Fd_Table[fileID].read_ptr = -1;
		Open_Fd_Table[fileID].write_ptr = -1;
	}

    return 0;
}

//
// Seek (Read) to the location specified by argument "loc", from the beginning of the file pointed by the file's ID
//
int ssfs_frseek(int fileID, int loc){

	if (fileID < 0 || fileID > MAX_FILES) {
		printf("Error: Incorrect fileID\n");
		return -1;
	}

	if (loc < 0) {
		printf("Error: Incorrect location\n");
		return -1;
	}


	if (Open_Fd_Table[fileID].inode_nb == -1) {
		printf("Error: Empty file descriptor\n");
		return -1;
	}

	// Need to check if location is bigger than file size
	if (get_file_size(Open_Fd_Table[fileID].inode_nb) < loc) {
		printf("Error: Location bigger than file size\n");
		return -1;
	}
	else {
		Open_Fd_Table[fileID].read_ptr = loc;
    	return 0;
    }
}

//
// Seek (Write) to the location specified by argument "loc", from the beginning of the file pointed by the file's ID
//
int ssfs_fwseek(int fileID, int loc){

	if (fileID < 0 || fileID > MAX_FILES) {
		printf("Error: Incorrect fileID\n");
		return -1;
	}

	if (loc < 0) {
		printf("Error: Incorrect location\n");
		return -1;
	}

	if (Open_Fd_Table[fileID].inode_nb == -1) {
		printf("Error: Empty file descriptor\n");
		return -1;
	}

	// Need to check if location is bigger than file size
	if (get_file_size(Open_Fd_Table[fileID].inode_nb) < loc) {
		printf("Error: Location bigger than file size\n");
		return -1;
	}
	else {
		Open_Fd_Table[fileID].write_ptr = loc;    
		return 0;
	}
}

//
// Write the string from "buf" of size "length" into the file pointed by the file's ID
//
int ssfs_fwrite(int fileID, char *buf, int length){

	if (length < 0) {
		return 0;
	}

	if (fileID < 0 || fileID > MAX_FILES) {
		printf("Error: Incorrect fileID\n");
		return 0;
	}

	// Get the file entry and file size
	Fd_entry file_entry = Open_Fd_Table[fileID];

	int block_write_nb = file_entry.write_ptr / SIZE_BLOCK;

	// Get root j node
	if (root_jnode == NULL) {
		getRootJNode();
	}
	int direct_ptr_nb = file_entry.inode_nb/(SIZE_BLOCK/sizeof(Node));
	int block_nb = (*root_jnode).direct_ptr[direct_ptr_nb];
	int inode_index_in_block = file_entry.inode_nb % (SIZE_BLOCK/sizeof(Node));
	int inode_nb = file_entry.inode_nb;
	if (block_nb > -1) {
		// Get i-node block
		Node* inode_block = (Node*) malloc(SIZE_BLOCK);
		read_blocks(DB_STARTING_ADDRESS + block_nb, 1, inode_block);
		// Get inode
		Node file_inode = inode_block[inode_index_in_block];
		////////////////////////////////////////////////
		// SPECIAL SCENARIO: Need to use indirect_ptr //
		////////////////////////////////////////////////
		if (block_write_nb > 13) {
			// Find an empty inode (like for open)
			for (int i=0; i<14; i++) {
				// Hold block of inodes
				Node* block_inode = (Node*) malloc(SIZE_BLOCK);
				// Unintialized block of i-nodes
				if ((*root_jnode).direct_ptr[i] == -1) {
					int inode_block_nb = find_empty_data_block();
					if (inode_block_nb == -1) {
						free(block_inode);
						return -1;
					}
					initialize_new_inode_block(inode_block_nb);
					// Add new inode block to jroot
					(*root_jnode).direct_ptr[i] = inode_block_nb;
					// update j root in SB
					updateSB();
				}
				read_blocks(DB_STARTING_ADDRESS + (*root_jnode).direct_ptr[i], 1, block_inode);
				// iterate through each individual inode in a block
				for (int x=0; x<SIZE_BLOCK/sizeof(Node); x++) {
					//found empty inode
					if (block_inode[x].size == -1) {
						// Change size to 0 to occupy it
						block_inode[x].size = 0;

						// Update i-node block in disk
						write_blocks(DB_STARTING_ADDRESS + (*root_jnode).direct_ptr[i], 1, block_inode);
						free(block_inode);
						// update new i-node in which we change the direct pointer
						file_inode.indirectPtr = i*SIZE_BLOCK/sizeof(Node) + x;

						memcpy(&(inode_block[inode_index_in_block]), &file_inode, sizeof(Node));
						write_blocks(DB_STARTING_ADDRESS + block_nb, 1, inode_block);

						inode_nb = file_inode.indirectPtr;

						block_write_nb = block_write_nb % 14;
						get_data_block(inode_nb);
						// Exit nested loop
						x = 500;
						i = 500;
					}

				}
			}	
		}
		/////////////////////
		// NORMAL SCENARIO //
		/////////////////////
		else if (file_inode.direct_ptr[block_write_nb] == -1)
			get_data_block(file_entry.inode_nb);
		free (inode_block);
	}
	else {
		printf("Error: Negative Block number\n");
		return 0;
	}

	char* block = (char*) malloc(SIZE_BLOCK);
	int data_block_nb = copy_data_block(inode_nb, block, block_write_nb);

	if (data_block_nb == -1) {
		printf("Error: Error getting block in ssfs_fwrite\n");
		free(block);
		return 0;
	}

	//////////////////////////////////////////////////////////////
	// Scenario 1: Wrting to a file with room left in the block //
	//////////////////////////////////////////////////////////////
	if ((file_entry.write_ptr % SIZE_BLOCK) + length  <= SIZE_BLOCK) {

		memcpy(&(block[file_entry.write_ptr%SIZE_BLOCK]), buf, length);
		// update file size
		update_file_size(inode_nb, file_entry.write_ptr, length);
		// update write pointer
		file_entry.write_ptr += length;
		Open_Fd_Table[fileID] = file_entry;

		write_blocks(DB_STARTING_ADDRESS + data_block_nb, 1, block);
		free(block);
   		return length;
	}
	////////////////////////////////////////////////////////////////////
	// Scenario 2: Wrting to a file with not enough room in the block //
	////////////////////////////////////////////////////////////////////
	else {
		int size_left_in_block = SIZE_BLOCK - (file_entry.write_ptr % SIZE_BLOCK);

		memcpy(&(block[file_entry.write_ptr%SIZE_BLOCK]), buf, size_left_in_block);

		// update file size
		update_file_size(inode_nb, file_entry.write_ptr, size_left_in_block);
		// update write pointer
		file_entry.write_ptr += size_left_in_block;
		Open_Fd_Table[fileID] = file_entry;

		write_blocks(DB_STARTING_ADDRESS + data_block_nb, 1, block);
		free(block);
		return size_left_in_block + ssfs_fwrite(fileID, &(buf[size_left_in_block]), length - size_left_in_block);
	}
}

//
// Read the string into "buf" of size "length" from the file pointed by the file's ID
//
int ssfs_fread(int fileID, char *buf, int length){

	if (length <= 0) {
		return 0;
	}

	if (fileID < 0 || fileID > MAX_FILES) {
		printf("Error: Incorrect fileID\n");
		return 0;
	}

	Fd_entry file_entry = Open_Fd_Table[fileID];
	int block_nb = -1;
	int inode_nb = file_entry.inode_nb;
	int block_read_nb = file_entry.read_ptr / SIZE_BLOCK;
	char* block = (char*) malloc(SIZE_BLOCK);
	// Need to use indirect pointer
	if (block_read_nb > 13) {
		block_read_nb = block_read_nb % 14;
		// Get indirect pointer i-node's number
		int direct_ptr_nb = inode_nb/(SIZE_BLOCK/sizeof(Node));
		int temp_block_nb = (*root_jnode).direct_ptr[direct_ptr_nb];
		int inode_nb_in_block = inode_nb % (SIZE_BLOCK/sizeof(Node));	
		if (temp_block_nb > -1) {
			// Get i-node block
			Node* inode_block = (Node*) malloc(SIZE_BLOCK);
			read_blocks(DB_STARTING_ADDRESS + temp_block_nb, 1, inode_block);			
			// Get i-node
			Node inode = inode_block[inode_nb_in_block];
			inode_nb = inode.indirectPtr;
			free(inode_block);
		}
		else {
			printf("Error: Negative Block number\n");
			return 0;
		}
		block_nb = copy_data_block(inode_nb, block, block_read_nb);
	}
	else {
		block_nb = copy_data_block(inode_nb, block, block_read_nb);
	}

	if (block_nb == -1) {
		printf("Error: Error getting block in ssfs_fread\n");
		free(block);
		return 0;
	}

	int size = get_file_size(inode_nb);
	if (length + file_entry.read_ptr > size) {
		printf("Error: Read length too big\n");
		// Reduce the length, as you can't read above the size
		length = size;
	}

	///////////////////////////////////////////
	// Scenario 1: Read from a single block  //
	///////////////////////////////////////////
	if ((file_entry.read_ptr%SIZE_BLOCK) + length < SIZE_BLOCK) {
		memcpy(buf, &(block[file_entry.read_ptr%SIZE_BLOCK]), length%SIZE_BLOCK);
		// update read pointer
		file_entry.read_ptr += length;
		Open_Fd_Table[fileID] = file_entry;

		free(block);
		return length;
	}
	///////////////////////////////////////////
	// Scenario 2: Read from multiple blocks //
	///////////////////////////////////////////
	else {
		int size_left_in_block = SIZE_BLOCK - (file_entry.read_ptr%SIZE_BLOCK);
		memcpy(buf, &(block[file_entry.read_ptr%SIZE_BLOCK]), size_left_in_block);

		// update read pointer
		file_entry.read_ptr += size_left_in_block;
		Open_Fd_Table[fileID] = file_entry;

		free(block);
		return size_left_in_block + ssfs_fread(fileID, &(buf[size_left_in_block]), length - size_left_in_block);		
	}
}

/*
/ Release i-node entry and the data blocks used. This function is USED ONLY FOR INDIRECT POINTERS
*/
void ssfs_remove_inode(int nb) {

	int inode_nb = nb;
	int direct_ptr[14];

	//////////////////////////
	// Release i-node entry //
    //////////////////////////
	int direct_ptr_nb = inode_nb/(SIZE_BLOCK/sizeof(Node));
	int block_nb = (*root_jnode).direct_ptr[direct_ptr_nb];
	int node_nb_in_block = inode_nb % (SIZE_BLOCK/sizeof(Node));
	if (root_jnode == NULL) {
		getRootJNode();
	}
	// Get i-node block
	Node* inode_block = (Node*) malloc(SIZE_BLOCK);
	read_blocks(DB_STARTING_ADDRESS + block_nb, 1, inode_block);

	// Reset specific i-node
	inode_block[node_nb_in_block].size = -1;
	// Check if there's an indirect pointer, if there is, need to delete everything related to it as well
	if (inode_block[node_nb_in_block].indirectPtr != -1) {
		ssfs_remove_inode(inode_block[node_nb_in_block].indirectPtr);
		modify_fbm(inode_block[node_nb_in_block].indirectPtr,0);
	}	
	inode_block[node_nb_in_block].indirectPtr = -1;
	for (int i=0; i<14; i++) {
		direct_ptr[i] = inode_block[node_nb_in_block].direct_ptr[i];
		inode_block[node_nb_in_block].direct_ptr[i] = -1;
	}
	// update in disk
	write_blocks(DB_STARTING_ADDRESS + block_nb, 1, inode_block);
	free(inode_block);

    //////////////////////////////////////////////
    // Release the data blocks used by the file //
    //////////////////////////////////////////////
	for (int i=0; i<14; i++) {
		if (direct_ptr[i] != -1) {
			modify_fbm(direct_ptr[i], 0);
		}
	}
}

/*
/ Remove a file from the file shadow system with the name specified by "file"
*/
int ssfs_remove(char *file){

	int inode_nb = -1;
	int direct_ptr[14];

	//////////////////////////////////////
	// Remove file from Open File Table //
	//////////////////////////////////////
	int fd_index = ssfs_fopen(file);
	if (fd_index != -1) {
		Open_Fd_Table[fd_index].inode_nb = -1;
		Open_Fd_Table[fd_index].write_ptr = -1;
		Open_Fd_Table[fd_index].read_ptr = -1;	
	}

	//////////////////////////////////////
	// Remove file from directory entry //
	//////////////////////////////////////
	for (int i=0; i<MAX_FILES; i++) {
		if (strcmp(file, root_dir_cache[i].filename) == 0) {
			inode_nb = root_dir_cache[i].inode_nb;
			// Reset values
			strcpy(root_dir_cache[i].filename, "");
			root_dir_cache[i].inode_nb = -1;
			// Update directory in disk
			update_directory_disk();
			break;
		}
	}

	//////////////////////////
	// Release i-node entry //
    //////////////////////////
	int direct_ptr_nb = inode_nb/(SIZE_BLOCK/sizeof(Node));
	int block_nb = (*root_jnode).direct_ptr[direct_ptr_nb];
	int node_nb_in_block = inode_nb % (SIZE_BLOCK/sizeof(Node));
	if (root_jnode == NULL) {
		getRootJNode();
	}
	// Get i-node block
	Node* inode_block = (Node*) malloc(SIZE_BLOCK);
	read_blocks(DB_STARTING_ADDRESS + block_nb, 1, inode_block);

	// Reset specific i-node
	inode_block[node_nb_in_block].size = -1;

	// Check if there's an indirect pointer, if there is, need to delete everything related to it as well
	if (inode_block[node_nb_in_block].indirectPtr != -1) {
		ssfs_remove_inode(inode_block[node_nb_in_block].indirectPtr);
		modify_fbm(inode_block[node_nb_in_block].indirectPtr,0);
	}

	inode_block[node_nb_in_block].indirectPtr = -1;
	for (int i=0; i<14; i++) {
		direct_ptr[i] = inode_block[node_nb_in_block].direct_ptr[i];
		inode_block[node_nb_in_block].direct_ptr[i] = -1;
	}
	// update in disk
	write_blocks(DB_STARTING_ADDRESS + block_nb, 1, inode_block);
	free(inode_block);

    //////////////////////////////////////////////
    // Release the data blocks used by the file //
    //////////////////////////////////////////////
	for (int i=0; i<14; i++) {
		if (direct_ptr[i] != -1) {
			modify_fbm(direct_ptr[i], 0);
		}
	}
    return 0;
}