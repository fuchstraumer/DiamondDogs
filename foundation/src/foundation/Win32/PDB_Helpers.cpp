#include "PDB_Helpers.hpp"
#define WIN32_MEAN_AND_LEAN
#include <windows.h>
#include <DbgHelp.h>
#pragma comment(lib, "dbghelp.lib")
#include <crtdbg.h>
#include <tchar.h>

template<class T>
static T StructCast(void* ptr, LONG offset = 0)
{
    return reinterpret_cast<T>(reinterpret_cast<intptr_t>(ptr) + offset);
}

using DebugInfoSignature = DWORD;
constexpr static DWORD k_CrRsdsSignature = 'SDSR';
struct RsdsHeader
{
    DebugInfoSignature Signature;
    GUID Guid;
    long Version;
    char Filename[1];
};

static bool PeDebugdirRva(PIMAGE_OPTIONAL_HEADER optional_header, DWORD& debug_dir_rva, DWORD& debug_dir_size)
{
    if (optional_header->Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        PIMAGE_OPTIONAL_HEADER64 optionalHeader64 = StructCast<PIMAGE_OPTIONAL_HEADER64>(optional_header);
        debug_dir_rva = optionalHeader64->DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].VirtualAddress;
        debug_dir_size = optionalHeader64->DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].Size;
    }
    else
    {
        PIMAGE_OPTIONAL_HEADER32 optionalHeader32 = StructCast<PIMAGE_OPTIONAL_HEADER32>(optional_header);
        debug_dir_rva = optionalHeader32->DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].VirtualAddress;
        debug_dir_size = optionalHeader32->DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].Size;
    }

    if (debug_dir_rva == 0 && debug_dir_size == 0)
    {
        return true;
    }
    else if (debug_dir_rva == 0 || debug_dir_size == 0)
    {
        return false;
    }

    return true;
}

static bool PeFileoffsetRva(PIMAGE_NT_HEADERS nt_headers, DWORD rva, DWORD& file_offset)
{
    bool found = false;
    PIMAGE_SECTION_HEADER sectionHeader = IMAGE_FIRST_SECTION(nt_headers);
    for (int i = 0; i < nt_headers->FileHeader.NumberOfSections; ++i, sectionHeader++)
    {
        DWORD sectionSize = sectionHeader->Misc.VirtualSize;
        if ((rva >= sectionHeader->VirtualAddress) && (rva < sectionHeader->VirtualAddress + sectionSize))
        {
            found = true;
            break;
        }
    }

    if (!found)
    {
        return false;
    }

    const int diff = static_cast<int>(sectionHeader->VirtualAddress - sectionHeader->PointerToRawData);
    file_offset = static_cast<DWORD>(rva - diff);
    return true;
}

static char* FindPdb(LPBYTE image_base, PIMAGE_DEBUG_DIRECTORY debug_dir)
{
    LPBYTE debugInfo = image_base + debug_dir->PointerToRawData;
    const DWORD debugInfoSize = debug_dir->SizeOfData;
    if (debugInfo == 0 || debugInfoSize == 0)
    {
        return nullptr;
    }

    if (IsBadReadPtr(debugInfo, debugInfoSize))
    {
        return nullptr;
    }

    if (debugInfoSize < sizeof(DebugInfoSignature))
    {
        return nullptr;
    }

    if (debug_dir->Type == IMAGE_DEBUG_TYPE_CODEVIEW)
    {
        DWORD signature = *reinterpret_cast<DWORD*>(debugInfo);
        if (signature == k_CrRsdsSignature)
        {
            RsdsHeader* info = reinterpret_cast<RsdsHeader*>(debugInfo);
            if (IsBadReadPtr(debugInfo, sizeof(RsdsHeader)))
            {
                return nullptr;
            }

            if (IsBadStringPtrA(reinterpret_cast<const char*>(info->Filename), UINT_MAX))
            {
                return nullptr;
            }

            return info->Filename;
        }
    }

    return nullptr;
}

static bool PdbReplace(const std::wstring& fname, const std::wstring& pdb_name, wchar_t* pdb_name_buffer, int pdb_name_len)
{
    HANDLE fp = nullptr;
    HANDLE filemap = nullptr;
    LPVOID mem = 0;
    bool result = false;
    do
    {

        fp = CreateFile(reinterpret_cast<LPCTSTR>(fname.c_str()), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        
        if ((fp == INVALID_HANDLE_VALUE) || (fp == nullptr))
        {
            break;
        }

        filemap = CreateFileMapping(fp, nullptr, PAGE_READWRITE, 0, 0, nullptr);
        if (filemap == nullptr)
        {
            break;
        }

        mem = MapViewOfFile(filemap, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        if (mem == nullptr)
        {
            break;
        }

        PIMAGE_DOS_HEADER dosHeader = StructCast<PIMAGE_DOS_HEADER>(mem);
        if (dosHeader == 0)
        {
            break;
        }

        if (IsBadReadPtr(dosHeader, sizeof(IMAGE_DOS_HEADER)))
        {
            break;
        }

        if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE)
        {
            break;
        }

        PIMAGE_NT_HEADERS ntHeaders = StructCast<PIMAGE_NT_HEADERS>(dosHeader, dosHeader->e_lfanew);
        if (ntHeaders == 0) {
            break;
        }

        if (IsBadReadPtr(ntHeaders, sizeof(ntHeaders->Signature)))
        {
            break;
        }

        if (ntHeaders->Signature != IMAGE_NT_SIGNATURE)
        {
            break;
        }

        if (IsBadReadPtr(&ntHeaders->FileHeader, sizeof(IMAGE_FILE_HEADER)))
        {
            break;
        }

        if (IsBadReadPtr(&ntHeaders->OptionalHeader, ntHeaders->FileHeader.SizeOfOptionalHeader))
        {
            break;
        }

        if (ntHeaders->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC && ntHeaders->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        {
            break;
        }

        PIMAGE_SECTION_HEADER sectionHeaders = IMAGE_FIRST_SECTION(ntHeaders);
        if (IsBadReadPtr(sectionHeaders, ntHeaders->FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER)))
        {
            break;
        }

        DWORD debugDirRva = 0;
        DWORD debugDirSize = 0;
        if (!PeDebugdirRva(&ntHeaders->OptionalHeader, debugDirRva, debugDirSize))
        {
            break;
        }

        if (debugDirRva == 0 || debugDirSize == 0)
        {
            break;
        }

        DWORD debugDirOffset = 0;
        if (!PeFileoffsetRva(ntHeaders, debugDirRva, debugDirOffset))
        {
            break;
        }

        PIMAGE_DEBUG_DIRECTORY debugDir = StructCast<PIMAGE_DEBUG_DIRECTORY>(mem, debugDirOffset);
        if (debugDir == 0)
        {
            break;
        }

        if (IsBadReadPtr(debugDir, debugDirSize))
        {
            break;
        }

        if (debugDirSize < sizeof(IMAGE_DEBUG_DIRECTORY))
        {
            break;
        }

        int numEntries = debugDirSize / sizeof(IMAGE_DEBUG_DIRECTORY);
        if (numEntries == 0)
        {
            break;
        }

        for (int i = 1; i <= numEntries; i++, debugDir++)
        {
            char* pdbFound = FindPdb(static_cast<LPBYTE>(mem), debugDir);
            if (pdbFound && strlen(pdbFound) >= pdb_name.length())
            {
                size_t len = strlen(pdbFound);
                memcpy_s(pdb_name_buffer, pdb_name_len, pdbFound, len);
                std::memset(pdbFound, '\0', len);
                memcpy_s(pdbFound, len, pdb_name.c_str(), pdb_name.length());
                result = true;
            }
        }

    } while(0);

    if (mem != nullptr)
    {
        UnmapViewOfFile(mem);
    }

    if (filemap != nullptr)
    {
        CloseHandle(filemap);
    }

    if ((fp != nullptr) && (fp != INVALID_HANDLE_VALUE))
    {
        CloseHandle(fp);
    }

    return result;
}

bool ProcessPDB(const std::wstring& fname, const std::wstring& pdbname)
{
    wchar_t origPdbFile[MAX_PATH];
    memset(origPdbFile, 0, sizeof(origPdbFile));
    bool result = PdbReplace(fname, pdbname, origPdbFile, sizeof(origPdbFile));
    result &= static_cast<bool>(CopyFileW(origPdbFile, pdbname.c_str(), 0));
    return result;
}
