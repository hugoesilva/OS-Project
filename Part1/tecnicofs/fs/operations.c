#include "operations.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int tfs_init() {
    state_init();

    /* create root inode */
    int root = inode_create(T_DIRECTORY);
    if (root != ROOT_DIR_INUM) {
        return -1;
    }

    return 0;
}

int tfs_destroy() {
    state_destroy();
    return 0;
}

static bool valid_pathname(char const *name) {
    return name != NULL && strlen(name) > 1 && name[0] == '/';
}


int tfs_lookup(char const *name) {
    if (!valid_pathname(name)) {
        return -1;
    }

    // skip the initial '/' character
    name++;

    return find_in_dir(ROOT_DIR_INUM, name);
}

int tfs_open(char const *name, int flags) {
    int inum;
    size_t offset;

    /* Checks if the path name is valid */
    if (!valid_pathname(name)) {
        return -1;
    }

    inum = tfs_lookup(name);
    if (inum >= 0) {
        /* The file already exists */
        inode_t *inode = inode_get(inum);
        if (inode == NULL) {
            return -1;
        }

        /* Truncate (if requested) */
        if (flags & TFS_O_TRUNC) {
            if (inode->i_size > 0) {
                if (data_blocks_free(inode) == -1) {
                    return -1;
                }
                inode->i_size = 0;
            }
        }
        /* Determine initial offset */
        if (flags & TFS_O_APPEND) {
            offset = inode->i_size;
        } else {
            offset = 0;
        }
    } else if (flags & TFS_O_CREAT) {
        /* The file doesn't exist; the flags specify that it should be created*/
        /* Create inode */
        inum = inode_create(T_FILE);
        if (inum == -1) {
            return -1;
        }
        /* Add entry in the root directory */
        if (add_dir_entry(ROOT_DIR_INUM, inum, name + 1) == -1) {
            inode_delete(inum);
            return -1;
        }
        offset = 0;
    } else {
        return -1;
    }

    /* Finally, add entry to the open file table and
     * return the corresponding handle */
    return add_to_open_file_table(inum, offset);

    /* Note: for simplification, if file was created with TFS_O_CREAT and there
     * is an error adding an entry to the open file table, the file is not
     * opened but it remains created */
}


int tfs_close(int fhandle) { return remove_from_open_file_table(fhandle); }

ssize_t tfs_write(int fhandle, void const *buffer, size_t to_write) {
    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1;
    }


    /* From the open file table entry, we get the inode */
    inode_t *inode = inode_get(file->of_inumber);
    if (inode == NULL) {
        return -1;
    }

    /* Determine how many bytes to write */

    if (to_write + file->of_offset > BLOCK_SIZE * (DIRECT_DATA_BLOCKS + INDIRECT_BLOCK_INDEX_NUM)) {
        to_write = BLOCK_SIZE * (DIRECT_DATA_BLOCKS + INDIRECT_BLOCK_INDEX_NUM) - file->of_offset;
        // deny writing more than the space we can offer
    }

    if (to_write > 0) {
        if (inode->i_size == 0) {

            int n_blocks = (int) to_write / BLOCK_SIZE + 1;
            // number of blocks needed

            if (to_write % BLOCK_SIZE == 0){
                n_blocks--;
            }

            // alloc direct data blocks we need
            if (n_blocks <= DIRECT_DATA_BLOCKS) {
                if (direct_blocks_alloc(inode, 0, n_blocks) == -1) {
                    pthread_rwlock_unlock(&(inode->rwl));
                    return -1;
                }
            }
            else {
                // alloc 10 direct blocks
                if (direct_blocks_alloc(inode, 0, DIRECT_DATA_BLOCKS) == -1) {
                    pthread_rwlock_unlock(&(inode->rwl));
                    return -1;
                }
                // alloc an indirect block
                inode->i_indirect_data_block = data_block_alloc();
                if (inode->i_indirect_data_block == -1) {
                    pthread_rwlock_unlock(&(inode->rwl));
                    return -1;
                }
                // get the block we have just allocated
                void *block = data_block_get(inode->i_indirect_data_block);
                // set all its values to 0 (indexes from 0 to INDIRECT_BLOCK_INDEX_NUM)
                memset(block, 0, BLOCK_SIZE);

                // alloc the remaining amount of needed blocks in the indirect block
                int remaining = n_blocks - DIRECT_DATA_BLOCKS;
                for (int i = 0; i < remaining && i < INDIRECT_BLOCK_INDEX_NUM; i++) {
                    ((int *)block)[i] = data_block_alloc();
                }
            }
        }
        else {
            // if the file already contains data

            // get the first block that will possibly be allocated
            int startingBlock = (int) inode->i_size/BLOCK_SIZE + 1;
            if (inode->i_size % BLOCK_SIZE == 0) {
                startingBlock--;
            }

            // get the amount of data in the last block considering inode->i_size != 0
            int lastBlockDataSize = inode->i_size % BLOCK_SIZE;
            if (lastBlockDataSize == 0) {
                lastBlockDataSize = BLOCK_SIZE;
            }
            int lastBlockFreeSpace = BLOCK_SIZE - lastBlockDataSize;

            // blocks to alloc
            int writeOnFirstBlock = ((int) to_write) - lastBlockFreeSpace;

            int n_blocks =  writeOnFirstBlock / BLOCK_SIZE + 1;
            if (writeOnFirstBlock % BLOCK_SIZE == 0 || writeOnFirstBlock < 0) {
                n_blocks--;
            }

            int remaining = n_blocks;
            if (startingBlock < DIRECT_DATA_BLOCKS) {
                if (n_blocks + startingBlock <= DIRECT_DATA_BLOCKS) {
                    if (direct_blocks_alloc(inode, startingBlock, n_blocks + startingBlock) == -1) {
                        pthread_rwlock_unlock(&(inode->rwl));
                        return -1;
                    }
                    remaining = 0;
                    //allocated all needed blocks
                }
                else {
                    if (direct_blocks_alloc(inode, startingBlock, DIRECT_DATA_BLOCKS) == -1) {
                        pthread_rwlock_unlock(&(inode->rwl));
                        return -1;
                    }
                    remaining = n_blocks - (DIRECT_DATA_BLOCKS - startingBlock);
                }
            }

            if (remaining > 0) {
                // if we still need to alloc more blocks than the direct ones

                // get the indirect block
                void *block = data_block_get(inode->i_indirect_data_block);
                if (block == NULL) {
                    // alloc indirect data block
                    inode->i_indirect_data_block = data_block_alloc();
                    if (inode->i_indirect_data_block == -1) {
                        pthread_rwlock_unlock(&(inode->rwl));
                        return -1;
                    }
                    // retry getting the indirect block and set all its values to 0 (indexes)
                    block = data_block_get(inode->i_indirect_data_block);
                    memset(block, 0, BLOCK_SIZE);

                    // go through all the indexes of the indirect 
                    // block and alloc the remaining amount of blocks
                    for (int i = 0; i < remaining && i < INDIRECT_BLOCK_INDEX_NUM; i++) {
                        ((int *)block)[i] = data_block_alloc();
                    }
                }
                else {
                    // indirect block already exists

                    // get the index of the first block we will possibly alloc
                    // from the indirect block
                    int startingIndirectIndex = startingBlock - DIRECT_DATA_BLOCKS;

                    // alloc the remaining amount of blocks
                    for (int i = startingIndirectIndex; i < remaining + startingIndirectIndex && i < INDIRECT_BLOCK_INDEX_NUM; i++) {
                        ((int *)block)[i] = data_block_alloc();
                    }
                }
            }
        }

        // number of the block we will start writing on
        int startWriting = (int) inode->i_size / BLOCK_SIZE;

        void *block;


        /* Perform the actual write */

        // obtain the free space from the first block we are going to write on
        int firstBlockFreeSpace = BLOCK_SIZE - ((int) inode->i_size % BLOCK_SIZE);


        // obtain the amount of blocks that are going to be written on
        int aux = (int) to_write - firstBlockFreeSpace;
        int blocksToWrite = (aux / BLOCK_SIZE) + 1;
        if ((aux % BLOCK_SIZE == 0) || (aux < 0)) {
            blocksToWrite--;
        }

        size_t leftToWrite = to_write;

        int buffOffset = 0;

        pthread_rwlock_wrlock(&(inode->rwl));

        for (int i = startWriting; i <= blocksToWrite + startWriting; i++) {
            if (leftToWrite == 0) {
                break;
            }
            if (i < DIRECT_DATA_BLOCKS) {
                block = data_block_get(inode->i_data_block[i]);
            }
            else {
                int *indirectBlock = data_block_get(inode->i_indirect_data_block);
                block = data_block_get(indirectBlock[i - DIRECT_DATA_BLOCKS]);
            }
            if (block == NULL) {
                pthread_rwlock_unlock(&(inode->rwl));
                return -1;
            }
            if (i == startWriting) {
                // if we are writing on the first block with free space, it means we have to use the left
                // space that exists in it
                if (leftToWrite > firstBlockFreeSpace) {
                    memcpy(block + (file->of_offset % BLOCK_SIZE), buffer + buffOffset, (size_t) firstBlockFreeSpace);

                    buffOffset += firstBlockFreeSpace;
                    leftToWrite -= (size_t) firstBlockFreeSpace;
                }
                else {
                    memcpy(block + (file->of_offset % BLOCK_SIZE), buffer + buffOffset, leftToWrite);
                    buffOffset += (int) leftToWrite;
                    leftToWrite = 0;
                }
            }
            else if (leftToWrite > BLOCK_SIZE) {
                // still need to write more than the size of an entire block
                memcpy(block, buffer + buffOffset, BLOCK_SIZE);
                buffOffset += BLOCK_SIZE;
                leftToWrite -= BLOCK_SIZE;
            }
            else {
                // need to write less or equals the size of an entire block
                memcpy(block, buffer + buffOffset, leftToWrite);
                buffOffset += (int) leftToWrite;
                leftToWrite = 0;
            }
        }
        pthread_rwlock_unlock(&(inode->rwl));

        // append fica no inode size depois de um close.

        /* The offset associated with the file handle is
         * incremented accordingly */
        file->of_offset += to_write;
        if (file->of_offset > inode->i_size) {
            inode->i_size = file->of_offset;
        }
    }
    return (ssize_t)to_write;
}


ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1;
    }

    /* From the open file table entry, we get the inode */
    inode_t *inode = inode_get(file->of_inumber);
    if (inode == NULL) {
        return -1;
    }

    /* Determine how many bytes to read */
    size_t to_read = inode->i_size - file->of_offset;
    if (to_read > len) {
        to_read = len;
    }

    // determine the block to start and finish reading at
    int startReading = (int) file->of_offset / BLOCK_SIZE;
    int endReading = (int) inode->i_size / BLOCK_SIZE;

    if (inode->i_size % BLOCK_SIZE == 0) {
        endReading--;
    }

    size_t leftToRead = to_read;

    void *block;

    int buffOffset = 0;

    if (to_read > 0) {
        pthread_rwlock_rdlock(&(inode->rwl));
        for (int i = startReading; i <= endReading; i++) {
            /* Perform the actual read */
            if (leftToRead == 0) {
                break;
            }

            // get the block
            if (i < DIRECT_DATA_BLOCKS) {
                block = data_block_get(inode->i_data_block[i]);
            }
            else {
                int *indirectBlock = data_block_get(inode->i_indirect_data_block);
                block = data_block_get(indirectBlock[i - DIRECT_DATA_BLOCKS]);
            }
            if (block == NULL) {
                pthread_rwlock_unlock(&(inode->rwl));
                return -1;
            }

            // if we are on the first block that is going to be read
            if (i == startReading) {
                size_t readFirstBlock = BLOCK_SIZE - (file->of_offset % BLOCK_SIZE);
                if (leftToRead >= readFirstBlock) {
                    memcpy(buffer + buffOffset, block + (file->of_offset % BLOCK_SIZE), readFirstBlock);
                    leftToRead -= readFirstBlock;
                    buffOffset += (int) readFirstBlock;
                }
                else {
                    memcpy(buffer + buffOffset, block + (file->of_offset % BLOCK_SIZE), leftToRead);
                    buffOffset += (int) leftToRead;
                    leftToRead = 0;
                }
            }
            else {
                if (leftToRead >= BLOCK_SIZE) {
                    memcpy(buffer + buffOffset, block, BLOCK_SIZE);
                    buffOffset += BLOCK_SIZE;
                    leftToRead -= BLOCK_SIZE;
                }
                else {
                    memcpy(buffer + buffOffset, block, leftToRead);
                    buffOffset += (int) leftToRead;
                    leftToRead = 0;
                }
            }
        }

        pthread_rwlock_unlock(&(inode->rwl));

        /* The offset associated with the file handle is
         * incremented accordingly */
        file->of_offset += to_read;
    }


    return (ssize_t)to_read;
}

int tfs_copy_to_external_fs(char const *source_path, char const *dest_path) {
    int inum;

    /* Checks if the path name is valid */
    if (!valid_pathname(source_path)) {
        return -1;
    }

    inode_t *inode;
    inum = tfs_lookup(source_path);
    inode = inode_get(inum);
    if (inode == NULL) {
        return -1;
    }

    remove(dest_path);

    FILE* fd = fopen(dest_path, "w");
    if (fd == NULL){
        return -1;
    }

    char buffer[inode->i_size / sizeof(char)];
    
    size_t leftToCopy = inode->i_size;

    // counter starts from block 0 and will go to the last block that will be copied
    int counter = 0;
    int buffOffset = 0;
    void* block;
    pthread_rwlock_rdlock(&(inode->rwl));
    while (leftToCopy > 0) {
        if (counter < DIRECT_DATA_BLOCKS) {
            block = data_block_get(inode->i_data_block[counter]);
            if (leftToCopy >= BLOCK_SIZE) {
                memcpy(buffer + buffOffset, block, BLOCK_SIZE);
                buffOffset += BLOCK_SIZE;
                leftToCopy -= BLOCK_SIZE;
            }
            else {
                memcpy(buffer + buffOffset, block, leftToCopy);
                buffOffset += (int) leftToCopy;
                leftToCopy = 0;
            }
        }
        else {
            int *indirectBlock = data_block_get(inode->i_indirect_data_block);
            block = data_block_get(indirectBlock[counter - DIRECT_DATA_BLOCKS]);
            if (leftToCopy >= BLOCK_SIZE) {
                memcpy(buffer + buffOffset, block, BLOCK_SIZE);
                buffOffset += BLOCK_SIZE;
                leftToCopy -= BLOCK_SIZE;
            }
            else {
                memcpy(buffer + buffOffset, block, leftToCopy);
                buffOffset += (int) leftToCopy;
                leftToCopy = 0;
            }
        }
        counter++;
    }
    pthread_rwlock_unlock(&(inode->rwl));

    /* write a string to the file */
    /* int bytes_written = write(fd, buffer, strlen(buffer)); */
    int bytes_written = (int) fwrite(buffer, sizeof(char), sizeof(buffer), fd);
    if (bytes_written < 0) {
        return -1;
    }

    /* close the file */
    fclose(fd);

    return 0;
}
