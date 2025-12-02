// Build: gcc -O2 -std=c17 -Wall -Wextra mkfs_builder.c -o mkfs_builder
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <time.h>

#define BS 4096u
#define INODE_SIZE 128u
#define ROOT_INO 1u
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
void crc32_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        CRC32_TAB[i] = c;
    }
}
uint32_t crc32(const void* data, size_t n) {
    const uint8_t* p = (const uint8_t*)data;
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++)
        c = CRC32_TAB[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
static uint32_t superblock_crc_finalize(superblock_t *sb) {
    sb->checksum = 0;
    uint32_t s = crc32((void *)sb, BS - 4);
    sb->checksum = s;
    return s;
}
void inode_crc_finalize(inode_t* ino) {
    uint8_t tmp[INODE_SIZE];
    memcpy(tmp, ino, INODE_SIZE);
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

// Manual LE conversion for structs
void superblock_to_le(superblock_t *sb) {
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
void inode_to_le(inode_t *in) {
    in->mode        = to_le16(in->mode);
    in->links       = to_le16(in->links);
    in->uid         = to_le32(in->uid);
    in->gid         = to_le32(in->gid);
    in->size_bytes  = to_le64(in->size_bytes);
    in->atime       = to_le64(in->atime);
    in->mtime       = to_le64(in->mtime);
    in->ctime       = to_le64(in->ctime);
    for (int i = 0; i < 12; i++) in->direct[i] = to_le32(in->direct[i]);
    in->reserved_0  = to_le32(in->reserved_0);
    in->reserved_1  = to_le32(in->reserved_1);
    in->reserved_2  = to_le32(in->reserved_2);
    in->proj_id     = to_le32(in->proj_id);
    in->uid16_gid16 = to_le32(in->uid16_gid16);
    in->xattr_ptr   = to_le64(in->xattr_ptr);
}
void dirent_to_le(dirent64_t *de) {
    de->ino = to_le32(de->ino);
}

int main(int argc, char* argv[]) {
    crc32_init();

    if (argc != 7) {
        fprintf(stderr, "Usage: %s --image <out.img> --size-kib <180..4096> --inodes <128..512>\n", argv[0]);
        fprintf(stderr, "Note: Size must be a multiple of 4\n");
        return 1;
    }

    const char* image_name = NULL;
    uint64_t size_kib = 0;
    uint64_t inode_count = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--image") == 0) image_name = argv[++i];
        else if (strcmp(argv[i], "--size-kib") == 0) size_kib = strtoull(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--inodes") == 0) inode_count = strtoull(argv[++i], NULL, 10);
    }

    //validation with error messages
    if (!image_name) {
        fprintf(stderr, "Error: Output image name is required\n");
        return 1;
    }

    if (size_kib < 180 || size_kib > 4096) {
        fprintf(stderr, "Error: Size must be between 180 and 4096 KiB (got %lu)\n", size_kib);
        return 1;
    }

    if (size_kib % 4 != 0) {
        fprintf(stderr, "Error: Size must be a multiple of 4 (got %lu)\n", size_kib);
        fprintf(stderr, "Valid sizes: 180, 184, 188, 192, ..., 4092, 4096\n");
        return 1;
    }

    if (inode_count < 128 || inode_count > 512) {
        fprintf(stderr, "Error: Inode count must be between 128 and 512 (got %lu)\n", inode_count);
        return 1;
    }

    
    uint64_t total_blocks = (size_kib * 1024u) / BS;
    uint64_t inode_table_blocks = (inode_count * INODE_SIZE + BS - 1) / BS;
    uint64_t inode_bitmap_start  = 1;
    uint64_t inode_bitmap_blocks = 1;
    uint64_t data_bitmap_start   = 2;
    uint64_t data_bitmap_blocks  = 1;
    uint64_t inode_table_start   = 3;
    uint64_t data_region_start   = inode_table_start + inode_table_blocks;
    uint64_t data_region_blocks  = total_blocks - data_region_start;


    superblock_t sb = {0};
    sb.magic              = 0x4D565346u; // 'MVSF'
    sb.version            = 1;
    sb.block_size         = BS;
    sb.total_blocks       = total_blocks;
    sb.inode_count        = inode_count;
    sb.inode_bitmap_start = inode_bitmap_start;
    sb.inode_bitmap_blocks= inode_bitmap_blocks;
    sb.data_bitmap_start  = data_bitmap_start;
    sb.data_bitmap_blocks = data_bitmap_blocks;
    sb.inode_table_start  = inode_table_start;
    sb.inode_table_blocks = inode_table_blocks;
    sb.data_region_start  = data_region_start;
    sb.data_region_blocks = data_region_blocks;
    sb.root_inode         = ROOT_INO;
    sb.mtime_epoch        = (uint64_t)time(NULL);
    sb.flags              = 0;

    // Convert to LE and finalize checksum
    superblock_to_le(&sb);
    superblock_crc_finalize(&sb);

    FILE *img = fopen(image_name, "wb");
    if (!img) { perror("fopen"); return 1; }

    uint8_t block[BS];

    //Block 0: Superblock
    memset(block, 0, BS);
    memcpy(block, &sb, sizeof(sb));
    fwrite(block, BS, 1, img);

    //Block 1: Inode bitmap (mark inode #1 used)
    memset(block, 0, BS);
    block[0] |= 0x01;
    fwrite(block, BS, 1, img);

    //Block 2: Data bitmap (mark first data block used)
    memset(block, 0, BS);
    block[0] |= 0x01;
    fwrite(block, BS, 1, img);

    //Inode table: first block contains root inode
    memset(block, 0, BS);
    inode_t root = {0};
    root.mode        = 0040000; 
    root.links       = 2;
    root.uid         = 0;
    root.gid         = 0;
    root.size_bytes  = 2 * sizeof(dirent64_t);
    root.atime       = sb.mtime_epoch;
    root.mtime       = sb.mtime_epoch;
    root.ctime       = sb.mtime_epoch;
    root.direct[0]   = (uint32_t)data_region_start;
    root.proj_id     = PROJECT_ID;
    root.uid16_gid16 = 0;
    root.xattr_ptr   = 0;

    inode_to_le(&root);
    inode_crc_finalize(&root);
    memcpy(block, &root, sizeof(root));
    fwrite(block, BS, 1, img);

    //Remaining inode table blocks zeroed
    memset(block, 0, BS);
    for (uint64_t i = 1; i < inode_table_blocks; i++)
        fwrite(block, BS, 1, img);

    //Data region: first block is root directory
    memset(block, 0, BS);
    dirent64_t dot = {0};
    dot.ino  = ROOT_INO;
    dot.type = 2; 
    strncpy(dot.name, ".", sizeof(dot.name));
    dirent_to_le(&dot);
    dirent_checksum_finalize(&dot);
    memcpy(block, &dot, sizeof(dot));

    dirent64_t dotdot = {0};
    dotdot.ino  = ROOT_INO;
    dotdot.type = 2;
    strncpy(dotdot.name, "..", sizeof(dotdot.name));
    dirent_to_le(&dotdot);
    dirent_checksum_finalize(&dotdot);
    memcpy(block + sizeof(dot), &dotdot, sizeof(dotdot));

    fwrite(block, BS, 1, img);

    //Remaining data blocks zeroed
    memset(block, 0, BS);
    for (uint64_t i = 1; i < data_region_blocks; i++)
        fwrite(block, BS, 1, img);

    fclose(img);

    printf("Filesystem image '%s' created successfully.\n", image_name);
    printf("Total blocks: %" PRIu64 "\n", total_blocks);
    printf("Inode count: %" PRIu64 "\n", inode_count);
    printf("Data region starts at block: %" PRIu64 "\n", data_region_start);

    return 0;
}
