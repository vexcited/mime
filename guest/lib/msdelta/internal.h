/* what the parts of the msdelta library share. */
#ifndef MIME_LIB_MSDELTA_INTERNAL_H
#define MIME_LIB_MSDELTA_INTERNAL_H

#include "msdelta.h"
#include <stdbool.h>
#ifdef _WIN32
#include <rtl.h>
#else
#include <string.h>
#endif

void *MdAlloc(size_t size);
void *MdAllocZero(size_t size);
void *MdRealloc(void *memory, size_t size);

/* ------------------------------------------------------------------ bit streams */

/* bits come least significant first. a stream starts with 3 bits that give the padding of its last byte. */
typedef struct MD_BITS {
    const uint8_t *Data;
    uint64_t Pos, End; /* in bits. */
    bool Failed;
} MD_BITS;

bool MdBitsOpen(MD_BITS *bits, const uint8_t *data, size_t size);
uint64_t MdBitsRead(MD_BITS *bits, unsigned count); /* up to 64 bits. */
/* a number: a unary count of nibbles, then that many nibbles plus one. */
uint64_t MdBitsReadNumber(MD_BITS *bits);
/* a size, a padding to a byte, and the bytes, which stay in the stream's memory. */
const uint8_t *MdBitsReadBuffer(MD_BITS *bits, size_t *size);
uint64_t MdBitsLeft(const MD_BITS *bits);

/* ------------------------------------------------------------------ huffman codes */

#define MD_HUFFMAN_MAX 16

typedef struct MD_HUFFMAN {
    uint16_t Count[MD_HUFFMAN_MAX + 1];
    uint32_t Start[MD_HUFFMAN_MAX + 1];
    uint16_t Offset[MD_HUFFMAN_MAX + 1];
    uint16_t *Sorted;
    unsigned Maximum;
} MD_HUFFMAN;

/* canonical codes in which the longest ones are the smallest numbers, sent with their first bit first. */
bool MdHuffmanBuild(MD_HUFFMAN *h, const uint8_t *lengths, unsigned symbols);
/* the symbol, or -1 when the stream ends or has no such code. */
int MdHuffmanDecode(const MD_HUFFMAN *h, MD_BITS *bits);
void MdHuffmanFree(MD_HUFFMAN *h);

/* the formats of the numbers of a rift table: a code of 252 symbols, sent as lengths. */
typedef struct MD_INT_FORMAT {
    MD_HUFFMAN Huffman;
} MD_INT_FORMAT;

bool MdIntFormatRead(MD_BITS *bits, MD_INT_FORMAT *format);
int64_t MdIntFormatNumber(const MD_INT_FORMAT *format, MD_BITS *bits);

/* ------------------------------------------------------------------ rift tables */

/* a rift table says how positions move: a pair of a left and a right position starts a stretch that ends before the next left. */
typedef struct MD_RIFT_ENTRY {
    int64_t Left, Right;
} MD_RIFT_ENTRY;

typedef struct MD_RIFT {
    MD_RIFT_ENTRY *Entries;
    size_t Count, Capacity;
} MD_RIFT;

void MdRiftFree(MD_RIFT *rift);
bool MdRiftAdd(MD_RIFT *rift, int64_t left, int64_t right);
void MdRiftSort(MD_RIFT *rift);
bool MdRiftCopy(MD_RIFT *to, const MD_RIFT *from);
/* the table of a stream: a flag, two formats for the numbers, a count and the pairs. */
bool MdRiftRead(MD_BITS *bits, MD_RIFT *rift);
bool MdRiftReadWith(MD_BITS *bits, MD_RIFT *rift, const MD_INT_FORMAT *lefts, const MD_INT_FORMAT *shifts);
/* the position moved by the stretch that holds it, and the last position of the stretch. */
int64_t MdRiftMap(const MD_RIFT *rift, int64_t position);
int64_t MdRiftGetRift(const MD_RIFT *rift, int64_t position, int64_t *last);
/* the table that moves a position by the first and then by the second. */
bool MdRiftMultiply(MD_RIFT *out, const MD_RIFT *a, const MD_RIFT *b);
bool MdRiftSum(MD_RIFT *out, const MD_RIFT *a, const MD_RIFT *b);
void MdRiftReduce(MD_RIFT *rift);
/* the table that undoes this one, for the positions from the start on. */
bool MdRiftReverse(MD_RIFT *out, const MD_RIFT *rift, int64_t start);

/* ------------------------------------------------------------------ the preparation of the source by file type */

/*
 * makes from the source what the target's compressor saw: a buffer of its own and a table of how the positions of the
 * source moved. raw sources are as they are.
 */
int MdPrepareSource(uint32_t file_type, uint64_t flags, const uint8_t *source, size_t source_size,
                    MD_BITS *preprocess, uint64_t target_size, uint8_t **prepared, size_t *prepared_size,
                    MD_RIFT *rift);

/* ------------------------------------------------------------------ portable executables */

struct MD_PE;
typedef struct MD_PE {
    uint8_t *Image; /* the file, which the transforms change. */
    uint32_t Size;
    bool Is32;
    uint16_t Machine;
    uint32_t Nt;           /* offset of the NT headers. */
    uint32_t Optional;     /* offset of the optional header. */
    uint32_t SectionTable; /* offset of the section headers. */
    uint16_t SectionCount;
    uint32_t HeadersSize; /* the size of the headers, not above the size of the file. */
    uint32_t DirectoryCount;
    uint32_t DirRva[16], DirOffset[16], DirSize[16];
    uint32_t ImportCount;   /* descriptors before the empty one. */
    uint32_t ImportOffset;  /* of the first, 0 when there are none. */
    uint32_t ExportOffset;  /* of the export directory, 0 when there is none. */
    uint32_t FunctionsOffset, FunctionCount;
    uint32_t NamesOffset, OrdinalsOffset, NameCount;
    uint32_t RelocOffset, RelocSize; /* the size is what the section leaves after the start, when that is larger. */
    uint32_t LoadConfigOffset;
} MD_PE;

/* parses the image, which stays the caller's. false when it is no executable the library can read. */
bool MdPeLoad(MD_PE *pe, uint8_t *image, uint32_t size);
uint64_t MdPeImageBase(const MD_PE *pe);
/* the offset in the file of a relative address that holds length bytes, or 0. */
uint32_t MdPeOffset(const MD_PE *pe, uint32_t rva, uint32_t length);
/* the number of bytes from a relative address to the end of the data of its section, or of the headers. */
uint32_t MdPeMaxSize(const MD_PE *pe, uint32_t rva);
/* file offsets to relative addresses, one stretch for the headers and one for each section with data. */
bool MdPeOffsetToRva(const MD_PE *pe, MD_RIFT *rift);

/* ------------------------------------------------------------------ metadata of the common language runtime */

/* where the streams of the metadata are, what its tables hold and how they are laid out. */
typedef struct MD_CLI {
    bool Present;
    uint32_t Offset, Size; /* of the metadata root, in the file. */
    uint32_t Base;         /* added to the addresses in the tables. */
    uint32_t Strings[2], UserStrings[2], Blobs[2], Guids[2], Tables[2]; /* offset and size of each stream. */
    bool WideStrings, WideGuids, WideBlobs;
    uint64_t Valid;
    uint32_t Rows[64];
    /* the layout, which follows from the others. */
    uint32_t TableOffset[64], RowSize[64];
    uint32_t ColumnOffset[64][16];
    bool WideColumn[64][16];
    uint32_t MaxCodedRows[13];
    bool LayoutOk;
} MD_CLI;

/* the maps that say where the rows and the heap entries of the source went in the target. */
typedef struct MD_CLI_MAP {
    MD_RIFT Strings, UserStrings, Blobs, Guids, Tables[64];
} MD_CLI_MAP;

bool MdCliFromImage(MD_CLI *cli, const uint8_t *image, uint32_t size, uint32_t offset, uint32_t length);
bool MdCliFromBits(MD_BITS *bits, MD_CLI *cli);
bool MdCliMapFromBits(MD_BITS *bits, MD_CLI_MAP *map);
void MdCliMapFree(MD_CLI_MAP *map);

/* an instruction of the amd64 processor: its length, where its address operand is, and what the operand is. */
typedef struct MD_INSN {
    uint8_t Length, Offset, Kind; /* the kind is 1 when it is no instruction, 5 for a displacement from the instruction pointer. */
} MD_INSN;

MD_INSN MdAmd64Disassemble(const uint8_t *code, size_t remaining);

typedef struct MD_PE_TARGET {
    uint64_t ImageBase;
    uint32_t GlobalPointer, TimeStamp;
    MD_RIFT RvaToOffset;
    bool HasCli;
    MD_CLI Cli;
    MD_CLI_MAP Map;
} MD_PE_TARGET;

/* the metadata of the source, the transforms that follow the map, and the rift table that goes with them. */
bool MdPeCli(const MD_PE *pe, MD_CLI *cli);
void MdPeMarkCliMethods(const MD_PE *pe, const MD_CLI *cli, uint8_t *hint);
void MdPeCliDisassemble(MD_PE *pe, const MD_CLI *cli, const MD_CLI_MAP *map);
void MdPeCliMetadata(MD_PE *pe, const MD_CLI *cli, const MD_CLI_MAP *map, const MD_RIFT *used, const MD_CLI *target);
bool MdPeCliCompressionRift(const MD_CLI *source, const MD_CLI *target, const MD_CLI_MAP *map, const MD_PE *transformed,
                            MD_RIFT *out);

/* the transforms: the source image takes the form the target will have, by the table of how relative addresses move. */
void MdPeUnbind(MD_PE *pe);
int MdPeTransform(MD_PE *pe, uint32_t file_type, uint64_t flags, const MD_RIFT *used, const MD_PE_TARGET *target);

/* the steps after the decoding: the x86 call translation when the flags ask for it, and the check of the hash. */
int MdFinishTarget(const MSDELTA_INFO *info, uint8_t *target, size_t size);

#endif
