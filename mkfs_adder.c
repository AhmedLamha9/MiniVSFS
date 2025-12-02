#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#define BS 4096u
#define INODE_SIZE 128u
#define ROOT_INO 1u
#define DIRECT_MAX 12
#define PROJECT_ID 9u

//little-endian conversion
static inline uint16_t to_le16(uint16_t x) {
    return ((x & 0x00FFu) << 8) | ((x & 0xFF00u) >> 8);
}
static inline uint32_t to_le32(uint32_t x) {
    return ((x & 0x000000FFu) << 24) |
           ((x & 0x0000FF00u) << 8)  |
           ((x & 0x00FF0000u) >> 8)  |
           ((x & 0xFF000000u) >> 24);
}
static inline uint64_t to_le64(uint64_t x) {
    return ((uint64_t)to_le32((uint32_t)(x & 0xFFFFFFFFu)) << 32) |
           (uint64_t)to_le32((uint32_t)(x >> 32));
}

static inline uint16_t from_le16(uint16_t x) { return to_le16(x); }
static inline uint32_t from_le32(uint32_t x) { return to_le32(x); }
static inline uint64_t from_le64(uint64_t x) { return to_le64(x); }

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t block_size;
    uint64_t total_blocks;
    uint64_t inode_count;
    uint64_t inode_bitmap_start;
    uint64_t inode_bitmap_blocks;
    uint64_t data_bitmap_start;
    uint64_t data_bitmap_blocks;
    uint64_t inode_table_start;
    uint64_t inode_table_blocks;
    uint64_t data_region_start;
    uint64_t data_region_blocks;
    uint64_t root_inode;
    uint64_t mtime_epoch;
    uint32_t flags;
    uint32_t checksum;
} superblock_t;
#pragma pack(pop)
_Static_assert(sizeof(superblock_t) <= BS, "superblock must fit in one block");

#pragma pack(push, 1)
typedef struct {
    uint16_t mode;
    uint16_t links;
    uint32_t uid;
    uint32_t gid;
    uint64_t size_bytes;
    uint64_t atime;
    uint64_t mtime;
    uint64_t ctime;
    uint32_t direct[12];
    uint32_t reserved_0;
    uint32_t reserved_1;
    uint32_t reserved_2;
    uint32_t proj_id;
    uint32_t uid16_gid16;
    uint64_t xattr_ptr;
    uint64_t inode_crc;
} inode_t;
#pragma pack(pop)
_Static_assert(sizeof(inode_t) == INODE_SIZE, "inode size mismatch");

#pragma pack(push, 1)
typedef struct {
    uint32_t ino;
    uint8_t type;
    char name[58];
    uint8_t checksum;
} dirent64_t;
#pragma pack(pop)
_Static_assert(sizeof(dirent64_t) == 64, "dirent size mismatch");

// CRC32 helpers
uint32_t CRC32_TAB[256];
void crc32_init(void){
    for (uint32_t i=0;i<256;i++){
        uint32_t c=i;
        for(int j=0;j<8;j++) c = (c&1)?(0xEDB88320u^(c>>1)):(c>>1);
        CRC32_TAB[i]=c;
    }
}
uint32_t crc32(const void* data, size_t n){
    const uint8_t* p=(const uint8_t*)data; uint32_t c=0xFFFFFFFFu;
    for(size_t i=0;i<n;i++) c = CRC32_TAB[(c^p[i])&0xFF] ^ (c>>8);
    return c ^ 0xFFFFFFFFu;
}

void superblock_crc_finalize(superblock_t *sb) {
    sb->checksum = 0;
    uint32_t s = crc32((void *)sb, BS - 4);
    sb->checksum = s;
}

void inode_crc_finalize(inode_t* ino){
    uint8_t tmp[INODE_SIZE]; memcpy(tmp, ino, INODE_SIZE);
    memset(&tmp[120], 0, 8);
    uint32_t c = crc32(tmp, 120);
    ino->inode_crc = (uint64_t)c;
}

void dirent_checksum_finalize(dirent64_t* de) {
    const uint8_t* p = (const uint8_t*)de;
    uint8_t x = 0;
    for (int i = 0; i < 63; i++) x ^= p[i];
    de->checksum = x;
}

// Conversion functions
void superblock_to_host(superblock_t *sb) {
    sb->magic = from_le32(sb->magic);
    sb->version = from_le32(sb->version);
    sb->block_size = from_le32(sb->block_size);
    sb->total_blocks = from_le64(sb->total_blocks);
    sb->inode_count = from_le64(sb->inode_count);
    sb->inode_bitmap_start = from_le64(sb->inode_bitmap_start);
    sb->inode_bitmap_blocks = from_le64(sb->inode_bitmap_blocks);
    sb->data_bitmap_start = from_le64(sb->data_bitmap_start);
    sb->data_bitmap_blocks = from_le64(sb->data_bitmap_blocks);
    sb->inode_table_start = from_le64(sb->inode_table_start);
    sb->inode_table_blocks = from_le64(sb->inode_table_blocks);
    sb->data_region_start = from_le64(sb->data_region_start);
    sb->data_region_blocks = from_le64(sb->data_region_blocks);
    sb->root_inode = from_le64(sb->root_inode);
    sb->mtime_epoch = from_le64(sb->mtime_epoch);
    sb->flags = from_le32(sb->flags);
    sb->checksum = from_le32(sb->checksum);
}

void superblock_to_disk(superblock_t *sb) {
    sb->magic              = to_le32(sb->magic);
    sb->version            = to_le32(sb->version);
    sb->block_size         = to_le32(sb->block_size);
    sb->total_blocks       = to_le64(sb->total_blocks);
    sb->inode_count        = to_le64(sb->inode_count);
    sb->inode_bitmap_start = to_le64(sb->inode_bitmap_start);
    sb->inode_bitmap_blocks= to_le64(sb->inode_bitmap_blocks);
    sb->data_bitmap_start  = to_le64(sb->data_bitmap_start);
    sb->data_bitmap_blocks = to_le64(sb->data_bitmap_blocks);
    sb->inode_table_start  = to_le64(sb->inode_table_start);
    sb->inode_table_blocks = to_le64(sb->inode_table_blocks);
    sb->data_region_start  = to_le64(sb->data_region_start);
    sb->data_region_blocks = to_le64(sb->data_region_blocks);
    sb->root_inode         = to_le64(sb->root_inode);
    sb->mtime_epoch        = to_le64(sb->mtime_epoch);
    sb->flags              = to_le32(sb->flags);
}

void inode_to_host(inode_t *in) {
    in->mode = from_le16(in->mode);
    in->links = from_le16(in->links);
    in->uid = from_le32(in->uid);
    in->gid = from_le32(in->gid);
    in->size_bytes = from_le64(in->size_bytes);
    in->atime = from_le64(in->atime);
    in->mtime = from_le64(in->mtime);
    in->ctime = from_le64(in->ctime);
    for (int i = 0; i < 12; i++) in->direct[i] = from_le32(in->direct[i]);
    in->reserved_0 = from_le32(in->reserved_0);
    in->reserved_1 = from_le32(in->reserved_1);
    in->reserved_2 = from_le32(in->reserved_2);
    in->proj_id = from_le32(in->proj_id);
    in->uid16_gid16 = from_le32(in->uid16_gid16);
    in->xattr_ptr = from_le64(in->xattr_ptr);
    in->inode_crc = from_le64(in->inode_crc);
}

void inode_to_disk(inode_t *in) {
    in->mode = to_le16(in->mode);
    in->links = to_le16(in->links);
    in->uid = to_le32(in->uid);
    in->gid = to_le32(in->gid);
    in->size_bytes = to_le64(in->size_bytes);
    in->atime = to_le64(in->atime);
    in->mtime = to_le64(in->mtime);
    in->ctime = to_le64(in->ctime);
    for (int i = 0; i < 12; i++) in->direct[i] = to_le32(in->direct[i]);
    in->reserved_0 = to_le32(in->reserved_0);
    in->reserved_1 = to_le32(in->reserved_1);
    in->reserved_2 = to_le32(in->reserved_2);
    in->proj_id = to_le32(in->proj_id);
    in->uid16_gid16 = to_le32(in->uid16_gid16);
    in->xattr_ptr = to_le64(in->xattr_ptr);
}

void dirent_to_host(dirent64_t *de) {
    de->ino = from_le32(de->ino);
}

void dirent_to_disk(dirent64_t *de) {
    de->ino = to_le32(de->ino);
}

// Find first free bit in bitmap
int find_free_bit(uint8_t *bitmap, int bitmap_size) {
    for (int i = 0; i < bitmap_size; i++) {
        if (bitmap[i] != 0xFF) {
            for (int j = 0; j < 8; j++) {
                if (!(bitmap[i] & (1 << j))) {
                    return i * 8 + j;
                }
            }
        }
    }
    return -1;
}

// Set bit in bitmap
void set_bit(uint8_t *bitmap, int bit) {
    int byte = bit / 8;
    int offset = bit % 8;
    bitmap[byte] |= (1 << offset);
}

// Clear bit in bitmap
void clear_bit(uint8_t *bitmap, int bit) {
    int byte = bit / 8;
    int offset = bit % 8;
    bitmap[byte] &= ~(1 << offset);
}

// Check if bit is set
int is_bit_set(uint8_t *bitmap, int bit) {
    int byte = bit / 8;
    int offset = bit % 8;
    return (bitmap[byte] & (1 << offset)) != 0;
}

int main(int argc, char *argv[]) {
    crc32_init();
    
    if (argc != 7) {
        fprintf(stderr, "Usage: %s --input <input.img> --output <output.img> --file <filename>\n", argv[0]);
        return 1;
    }

    const char *input_name = NULL;
    const char *output_name = NULL;
    const char *file_name = NULL;

    //Parse command line args
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--input") == 0) input_name = argv[++i];
        else if (strcmp(argv[i], "--output") == 0) output_name = argv[++i];
        else if (strcmp(argv[i], "--file") == 0) file_name = argv[++i];
    }

    if (!input_name || !output_name || !file_name) {
        fprintf(stderr, "Missing required arguments\n");
        return 1;
    }

    //Check filename length before any processing
    if (strlen(file_name) > 57) {
        fprintf(stderr, "Error: Filename '%s' too long (max 57 characters)\n", file_name);
        return 1;
    }

    //Open input file system image
    FILE *input_img = fopen(input_name, "rb");
    if (!input_img) {
        perror("Failed to open input image");
        return 1;
    }

    //Create output file
    FILE *output_img = fopen(output_name, "wb");
    if (!output_img) {
        perror("Failed to create output image");
        fclose(input_img);
        return 1;
    }

    //Copy input file to output file first
    uint8_t copy_buffer[BS];
    size_t bytes_read;
    rewind(input_img);
    while ((bytes_read = fread(copy_buffer, 1, BS, input_img)) > 0) {
        if (fwrite(copy_buffer, 1, bytes_read, output_img) != bytes_read) {
            perror("Failed to copy input to output");
            fclose(input_img);
            fclose(output_img);
            return 1;
        }
    }
    fclose(input_img);

    
    FILE *modify_img = fopen(output_name, "rb+");
    if (!modify_img) {
        perror("Failed to open output image for modification");
        fclose(output_img);
        return 1;
    }
    fclose(output_img);

    //Read superblock from output file
    superblock_t sb;
    if (fread(&sb, sizeof(sb), 1, modify_img) != 1) {
        perror("Failed to read superblock");
        fclose(modify_img);
        return 1;
    }
    superblock_to_host(&sb);

    //Verify magic number
    if (sb.magic != 0x4D565346) {
        fprintf(stderr, "Invalid filesystem magic number\n");
        fclose(modify_img);
        return 1;
    }

    //Read inode bitmap
    uint8_t inode_bitmap[BS];
    fseek(modify_img, sb.inode_bitmap_start * BS, SEEK_SET);
    if (fread(inode_bitmap, BS, 1, modify_img) != 1) {
        perror("Failed to read inode bitmap");
        fclose(modify_img);
        return 1;
    }

    //Read data bitmap
    uint8_t data_bitmap[BS];
    fseek(modify_img, sb.data_bitmap_start * BS, SEEK_SET);
    if (fread(data_bitmap, BS, 1, modify_img) != 1) {
        perror("Failed to read data bitmap");
        fclose(modify_img);
        return 1;
    }

    //Find free inode
    int free_inode = find_free_bit(inode_bitmap, BS);
    if (free_inode == -1) {
        fprintf(stderr, "No free inodes available\n");
        fclose(modify_img);
        return 1;
    }

    //Open the file to add
    FILE *file_to_add = fopen(file_name, "rb");
    if (!file_to_add) {
        perror("Failed to open file to add");
        fclose(modify_img);
        return 1;
    }

    //Get file size
    fseek(file_to_add, 0, SEEK_END);
    uint64_t file_size = ftell(file_to_add);
    fseek(file_to_add, 0, SEEK_SET);

    //Calculate required data blocks
    uint64_t blocks_needed = (file_size + BS - 1) / BS;
    if (blocks_needed > DIRECT_MAX) {
        fprintf(stderr, "File too large: requires %lu blocks, maximum is %d\n", blocks_needed, DIRECT_MAX);
        fclose(file_to_add);
        fclose(modify_img);
        return 1;
    }

    //Find free data blocks
    uint32_t data_blocks[DIRECT_MAX] = {0};
    for (uint64_t i = 0; i < blocks_needed; i++) {
        int free_block = find_free_bit(data_bitmap, BS);
        if (free_block == -1) {
            fprintf(stderr, "Not enough free data blocks\n");
            fclose(file_to_add);
            fclose(modify_img);
            return 1;
        }
        data_blocks[i] = sb.data_region_start + free_block;
        set_bit(data_bitmap, free_block);
    }

    //Read root inode
    inode_t root_inode;
    fseek(modify_img, sb.inode_table_start * BS, SEEK_SET);
    if (fread(&root_inode, sizeof(root_inode), 1, modify_img) != 1) {
        perror("Failed to read root inode");
        fclose(file_to_add);
        fclose(modify_img);
        return 1;
    }
    inode_to_host(&root_inode);

    //Read root directory block
    uint8_t root_dir_block[BS];
    fseek(modify_img, root_inode.direct[0] * BS, SEEK_SET);
    if (fread(root_dir_block, BS, 1, modify_img) != 1) {
        perror("Failed to read root directory");
        fclose(file_to_add);
        fclose(modify_img);
        return 1;
    }

    
    dirent64_t *entries = (dirent64_t *)root_dir_block;
    size_t max_entries = BS / sizeof(dirent64_t);
    int free_entry = -1;
    int file_exists = 0;

    for (size_t i = 0; i < max_entries; i++) {
        dirent_to_host(&entries[i]);
        
        //Check if file already exists
        if (entries[i].ino != 0 && strcmp(entries[i].name, file_name) == 0) {
            file_exists = 1;
            break;
        }
        
        //Find first free entry
        if (entries[i].ino == 0 && free_entry == -1) {
            free_entry = i;
        }
    }

    if (file_exists) {
        fprintf(stderr, "Error: File '%s' already exists in root directory\n", file_name);
        fclose(file_to_add);
        fclose(modify_img);
        return 1;
    }

    if (free_entry == -1) {
        fprintf(stderr, "No free directory entries in root\n");
        fclose(file_to_add);
        fclose(modify_img);
        return 1;
    }

    //Create new file inode
    inode_t new_inode = {0};
    new_inode.mode = 0100000; 
    new_inode.links = 1;
    new_inode.uid = 0;
    new_inode.gid = 0;
    new_inode.size_bytes = file_size;
    new_inode.atime = time(NULL);
    new_inode.mtime = time(NULL);
    new_inode.ctime = time(NULL);
    memcpy(new_inode.direct, data_blocks, sizeof(uint32_t) * DIRECT_MAX);
    new_inode.proj_id = PROJECT_ID;
    new_inode.uid16_gid16 = 0;
    new_inode.xattr_ptr = 0;

    inode_to_disk(&new_inode);
    inode_crc_finalize(&new_inode);

    //Write new inode to inode table
    fseek(modify_img, sb.inode_table_start * BS + free_inode * INODE_SIZE, SEEK_SET);
    if (fwrite(&new_inode, sizeof(new_inode), 1, modify_img) != 1) {
        perror("Failed to write new inode");
        fclose(file_to_add);
        fclose(modify_img);
        return 1;
    }

    //Mark inode as allocated
    set_bit(inode_bitmap, free_inode);
    fseek(modify_img, sb.inode_bitmap_start * BS, SEEK_SET);
    if (fwrite(inode_bitmap, BS, 1, modify_img) != 1) {
        perror("Failed to update inode bitmap");
        fclose(file_to_add);
        fclose(modify_img);
        return 1;
    }

    //Write file data to data blocks
    uint8_t file_buffer[BS];
    for (uint64_t i = 0; i < blocks_needed; i++) {
        size_t bytes_read = fread(file_buffer, 1, BS, file_to_add);
        if (bytes_read < BS && ferror(file_to_add)) {
            perror("Failed to read file");
            fclose(file_to_add);
            fclose(modify_img);
            return 1;
        }
        
        //Pad last block with zeros if needed
        if (bytes_read < BS) {
            memset(file_buffer + bytes_read, 0, BS - bytes_read);
        }
        
        fseek(modify_img, data_blocks[i] * BS, SEEK_SET);
        if (fwrite(file_buffer, BS, 1, modify_img) != 1) {
            perror("Failed to write file data");
            fclose(file_to_add);
            fclose(modify_img);
            return 1;
        }
    }

    //Update data bitmap
    fseek(modify_img, sb.data_bitmap_start * BS, SEEK_SET);
    if (fwrite(data_bitmap, BS, 1, modify_img) != 1) {
        perror("Failed to update data bitmap");
        fclose(file_to_add);
        fclose(modify_img);
        return 1;
    }

    //Add directory entry
    dirent64_t new_entry = {0};
    new_entry.ino = free_inode + 1; // Inodes are 1-indexed
    new_entry.type = 1; 
    strncpy(new_entry.name, file_name, sizeof(new_entry.name) - 1);
    new_entry.name[sizeof(new_entry.name) - 1] = '\0';
    
    dirent_to_disk(&new_entry);
    dirent_checksum_finalize(&new_entry);

    memcpy(&entries[free_entry], &new_entry, sizeof(new_entry));
    fseek(modify_img, root_inode.direct[0] * BS, SEEK_SET);
    if (fwrite(root_dir_block, BS, 1, modify_img) != 1) {
        perror("Failed to update root directory");
        fclose(file_to_add);
        fclose(modify_img);
        return 1;
    }

    //Update root inode size, link count, and timestamps
    root_inode.size_bytes += sizeof(dirent64_t);
    root_inode.links += 1;  //New file's .. points to root
    root_inode.mtime = time(NULL);
    root_inode.ctime = time(NULL);
    
    inode_to_disk(&root_inode);
    inode_crc_finalize(&root_inode);
    
    fseek(modify_img, sb.inode_table_start * BS, SEEK_SET);
    if (fwrite(&root_inode, sizeof(root_inode), 1, modify_img) != 1) {
        perror("Failed to update root inode");
        fclose(file_to_add);
        fclose(modify_img);
        return 1;
    }

    //Update superblock checksum after all modifications
    sb.mtime_epoch = time(NULL);
    superblock_to_disk(&sb);
    superblock_crc_finalize(&sb);
    
    
    fseek(modify_img, 0, SEEK_SET);
    if (fwrite(&sb, sizeof(sb), 1, modify_img) != 1) {
        perror("Failed to update superblock");
        fclose(file_to_add);
        fclose(modify_img);
        return 1;
    }

    
    fclose(file_to_add);
    fclose(modify_img);

    printf("File '%s' added successfully to inode %d\n", file_name, free_inode + 1);
    printf("File size: %lu bytes, %lu blocks\n", file_size, blocks_needed);
    printf("Output saved to: %s\n", output_name);

    return 0;
}
