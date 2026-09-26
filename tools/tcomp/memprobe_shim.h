// Шим для проверки src/runtime/MemProbe.cpp под g++.
//
// ЗАЧЕМ. MemProbe — фундамент рантайма (чтение/запись чужой памяти, границы
// секций образа), и до 85.24 он не проверялся вообще: в общем шиме нет
// PE-заголовков и констант защиты страниц, поэтому файл просто не собирался.
// В 85.24 в этот файл добавлены ворота записи (BlockWritesFor/WritesOpen и
// гейт внутри WrSafe) — молча ошибиться в фундаменте дороже всего.
//
// Здесь только недостающие объявления. Всё остальное берётся из общего шима.
#pragma once

#include "windows.h"

#ifndef PAGE_NOACCESS
#define PAGE_NOACCESS 0x01
#endif

// ---- минимальные PE-структуры (нужны ровно настолько, чтобы файл разобрался).
// Настоящие заголовки в SDK; здесь важно только, что поля называются так же.
typedef struct _IMAGE_DOS_HEADER_LOCAL {
    WORD  e_magic;
    WORD  e_cblp;
    WORD  e_cp;
    WORD  e_crlc;
    WORD  e_cparhdr;
    WORD  e_minalloc;
    WORD  e_maxalloc;
    WORD  e_ss;
    WORD  e_sp;
    WORD  e_csum;
    WORD  e_ip;
    WORD  e_cs;
    WORD  e_lfarlc;
    WORD  e_ovno;
    WORD  e_res[4];
    WORD  e_oemid;
    WORD  e_oeminfo;
    WORD  e_res2[10];
    LONG  e_lfanew;
} IMAGE_DOS_HEADER;

typedef struct _IMAGE_FILE_HEADER_LOCAL {
    WORD  Machine;
    WORD  NumberOfSections;
    DWORD TimeDateStamp;
    DWORD PointerToSymbolTable;
    DWORD NumberOfSymbols;
    WORD  SizeOfOptionalHeader;
    WORD  Characteristics;
} IMAGE_FILE_HEADER;

typedef struct _IMAGE_SECTION_HEADER_LOCAL {
    BYTE  Name[8];
    union { DWORD VirtualSize; DWORD PhysicalAddress; } Misc;
    DWORD VirtualAddress;
    DWORD SizeOfRawData;
    DWORD PointerToRawData;
    DWORD PointerToRelocations;
    DWORD PointerToLinenumbers;
    WORD  NumberOfRelocations;
    WORD  NumberOfLinenumbers;
    DWORD Characteristics;
} IMAGE_SECTION_HEADER;

#define IMAGE_SIZEOF_SHORT_NAME 8

#ifndef GetModuleHandle
#define GetModuleHandle GetModuleHandleA
#endif
#define IMAGE_SCN_MEM_EXECUTE 0x20000000

// PE32 optional header: поля по настоящим смещениям (для 32-битного образа).
typedef struct _IMAGE_OPTIONAL_HEADER32_LOCAL {
    WORD  Magic;
    BYTE  MajorLinkerVersion;
    BYTE  MinorLinkerVersion;
    DWORD SizeOfCode;
    DWORD SizeOfInitializedData;
    DWORD SizeOfUninitializedData;
    DWORD AddressOfEntryPoint;
    DWORD BaseOfCode;
    DWORD BaseOfData;
    DWORD ImageBase;
    DWORD SectionAlignment;
    DWORD FileAlignment;
    WORD  MajorOperatingSystemVersion;
    WORD  MinorOperatingSystemVersion;
    WORD  MajorImageVersion;
    WORD  MinorImageVersion;
    WORD  MajorSubsystemVersion;
    WORD  MinorSubsystemVersion;
    DWORD Win32VersionValue;
    DWORD SizeOfImage;
    DWORD SizeOfHeaders;
    DWORD CheckSum;
    WORD  Subsystem;
    WORD  DllCharacteristics;
    DWORD SizeOfStackReserve;
    DWORD SizeOfStackCommit;
    DWORD SizeOfHeapReserve;
    DWORD SizeOfHeapCommit;
    DWORD LoaderFlags;
    DWORD NumberOfRvaAndSizes;
    DWORD DataDirectory[16 * 2];
} IMAGE_OPTIONAL_HEADER32;

typedef struct _IMAGE_NT_HEADERS_LOCAL {
    DWORD Signature;
    IMAGE_FILE_HEADER FileHeader;
    IMAGE_OPTIONAL_HEADER32 OptionalHeader;
} IMAGE_NT_HEADERS;

#ifndef IMAGE_FIRST_SECTION
#define IMAGE_FIRST_SECTION(nt) \
    ((IMAGE_SECTION_HEADER*)((BYTE*)(nt) + 4 + sizeof(IMAGE_FILE_HEADER) \
                              + sizeof(IMAGE_OPTIONAL_HEADER32)))
#endif
