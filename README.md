# MiniVSFS: A Filesystem in C 🗄️

> A complete inode-based filesystem implementation demonstrating kernel-level programming with exact byte-level specification compliance, integrity guarantees, and robust error handling.

##  Project Impact

Built a functional filesystem from zero to binary, implementing the exact techniques used in Linux's ext4 and Windows' NTFS. This project demonstrates the practical implementation of core OS concepts that are typically only studied theoretically: inodes, bitmaps, superblocks, and file allocation strategies.

##  Quick Stats

| Metric | Value |
|--------|-------|
| Lines of Code | 900+ |
| Block Size | 4096 bytes |
| Max File Size | 48 KB |
| Integrity Features | CRC32 + XOR checksums |
| Compilation Warnings | Zero |

##  Core Features

### **mkfs_builder** - Filesystem Creator

- Creates complete filesystem images from CLI parameters
- Configurable size (180-4096 KiB) and inode count (128-512)
- Initializes superblock, bitmaps, root directory with integrity checks
- All metadata in little-endian format for cross-platform compatibility

### **mkfs_adder** - File Manager

- Adds files to existing filesystem images
- First-fit bitmap allocation (O(n) scanning)
- Atomic operations (preserves original on failure)
- Validates filename length, duplicates, space availability
- Updates all metadata and recalculates checksums

##  Technical Architecture

### Filesystem Layout

```text
Block 0: Superblock (4096 bytes)
Block 1: Inode Bitmap (1 block)
Block 2: Data Bitmap (1 block)
Blocks 3+: Inode Table (128-byte inodes)
Remaining: Data Blocks
```

### Key Data Structures

```c
// Superblock - filesystem metadata
typedef struct {
    uint32_t magic;      // 0x4D565346 ('MVSF')
    uint32_t version;    // Version 1
    uint32_t block_size; // 4096
    uint64_t total_blocks;
    // ... 15 more fields
    uint32_t checksum;   // CRC32
} superblock_t;

// Inode - 128-byte file metadata
typedef struct {
    uint16_t mode;       // 0040000=dir, 0100000=file
    uint16_t links;      // Hard link count
    uint64_t size_bytes;
    uint32_t direct[12]; // Direct block pointers
    uint64_t inode_crc;  // CRC32 checksum
} inode_t;

// Directory Entry - 64 bytes
typedef struct {
    uint32_t ino;        // Inode number (1-indexed)
    uint8_t type;        // 1=file, 2=dir
    char name[58];       // Null-terminated
    uint8_t checksum;    // XOR checksum
} dirent64_t;
```

##  Technical Highlights

### 1. Data Integrity System

- CRC32 checksums on superblock and inodes (same as PNG/ZIP files)
- XOR checksums on directory entries for lightweight verification
- Automatic recalculation after every modification

### 2. Cross-Platform Compatibility

- Manual little-endian conversion for all multi-byte fields
- Works on x86, ARM, PowerPC, RISC-V
- Packed structs guarantee exact on-disk layout

### 3. Efficient Bitmap Allocation

```c
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
```

- First-fit policy for inode and data block allocation
- 4KB bitmap tracks 32,768 blocks (128MB of data)


### 4. Robust Error Handling

- 24 specific error conditions with meaningful messages
- Graceful resource cleanup on failure
- Validation before modification (filename length, duplicates, space)

##  Quick Start

### Build

```bash
gcc -O2 -std=c17 -Wall -Wextra mkfs_builder.c -o mkfs_builder
gcc -O2 -std=c17 -Wall -Wextra mkfs_adder.c -o mkfs_adder
```

### Create Filesystem

```bash
./mkfs_builder --image disk.img --size-kib 1024 --inodes 256
```

### Add Files

```bash
./mkfs_adder --input disk.img --output disk_v2.img --file data.txt
```

### Verify

```bash
hexdump -C disk_v2.img | head -20  # Check magic number & structure
```

##  Key Technical Skills


- Direct disk I/O and binary file manipulation
- Memory layout management with packed structs
- Bit manipulation for resource tracking
- Defensive programming with comprehensive validation
- Atomic operations (copy-on-write semantics)
- Clean, modular code with zero warnings
- Endianness handling for cross-platform compatibility
- Cache-friendly data structures (64-byte aligned)
- Efficient bitmap algorithms for resource allocation


##  Completed as part of CSE321 - Operating Systems Lab (Summer 2025)


