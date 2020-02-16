#include "PDB_Helpers.hpp"
#define WIN32_MEAN_AND_LEAN
#include <windows.h>
#include <DbgHelp.h>
#pragma comment(lib, "dbghelp.lib")
#include <crtdbg.h>
#include <tchar.h>

template<class T>
static T struct_cast(void* ptr, LONG offset = 0)
{
    return reinterpret_cast<T>(reinterpret_cast<intptr_t>(ptr) + offset);
}

using DebugInfoSignature = DWORD;
constexpr static DWORD CR_RSDS_SIGNATURE = 'SDSR';
struct rsds_header
{
    DebugInfoSignature Signature;
    GUID guid;
    long version;
    char filename[1];
};

static bool pe_debugdir_rva(PIMAGE_OPTIONAL_HEADER optionalHeader, DWORD& debugDirRva, DWORD& debugDirSize)
{
    if (optionalHeader->Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        PIMAGE_OPTIONAL_HEADER64 optionalHeader64 = struct_cast<PIMAGE_OPTIONAL_HEADER64>(optionalHeader);
        debugDirRva = optionalHeader64->DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].VirtualAddress;
        debugDirSize = optionalHeader64->DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].Size;
    }
    else
    {
        PIMAGE_OPTIONAL_HEADER32 optionalHeader32 = struct_cast<PIMAGE_OPTIONAL_HEADER32>(optionalHeader);
        debugDirRva = optionalHeader32->DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].VirtualAddress;
        debugDirSize = optionalHeader32->DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].Size;
    }

    if (debugDirRva == 0 && debugDirSize == 0)
    {
        return true;
    }
    else if (debugDirRva == 0 || debugDirSize == 0)
    {
        return false;
    }

    return true;
}

static bool pe_fileoffset_rva(PIMAGE_NT_HEADERS ntHeaders, DWORD rva, DWORD& fileOffset)
{
    bool found = false;
    PIMAGE_SECTION_HEADER sectionHeader = IMAGE_FIRST_SECTION(ntHeaders);
    for (int i = 0; i < ntHeaders->FileHeader.NumberOfSections; ++i, sectionHeader++)
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
    fileOffset = static_cast<DWORD>(rva - diff);
    return true;
}

static char* find_pdb(LPBYTE imageBase, PIMAGE_DEBUG_DIRECTORY debugDir)
{
    LPBYTE debugInfo = imageBase + debugDir->PointerToRawData;
    const DWORD debugInfoSize = debugDir->SizeOfData;
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

    if (debugDir->Type == IMAGE_DEBUG_TYPE_CODEVIEW)
    {
        DWORD signature = *(DWORD*)debugInfo;
        if (signature == CR_RSDS_SIGNATURE)
        {
            rsds_header* info = (rsds_header*)(debugInfo);
            if (IsBadReadPtr(debugInfo, sizeof(rsds_header)))
            {
                return nullptr;
            }

            if (IsBadStringPtrA((const char*)info->filename, UINT_MAX))
            {
                return nullptr;
            }

            return info->filename;
        }
    }

    return nullptr;
}

static bool pdb_replace(const std::wstring& fname, const std::wstring& pdb_name, wchar_t* pdb_name_buffer, int pdb_name_len)
{
    HANDLE fp = nullptr;
    HANDLE filemap = nullptr;
    LPVOID mem = 0;
    bool result = false;
    do
    {

        fp = CreateFile((LPCTSTR)fname.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
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

        PIMAGE_DOS_HEADER dosHeader = struct_cast<PIMAGE_DOS_HEADER>(mem);
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

        PIMAGE_NT_HEADERS ntHeaders = struct_cast<PIMAGE_NT_HEADERS>(dosHeader, dosHeader->e_lfanew);
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
        if (!pe_debugdir_rva(&ntHeaders->OptionalHeader, debugDirRva, debugDirSize))
        {
            break;
        }

        if (debugDirRva == 0 || debugDirSize == 0)
        {
            break;
        }

        DWORD debugDirOffset = 0;
        if (!pe_fileoffset_rva(ntHeaders, debugDirRva, debugDirOffset))
        {
            break;
        }

        PIMAGE_DEBUG_DIRECTORY debugDir = struct_cast<PIMAGE_DEBUG_DIRECTORY>(mem, debugDirOffset);
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
            char* pdb_found = find_pdb((LPBYTE)mem, debugDir);
            if (pdb_found && strlen(pdb_found) >= pdb_name.length())
            {
                size_t len = strlen(pdb_found);
                memcpy_s(pdb_name_buffer, pdb_name_len, pdb_found, len);
                std::memset(pdb_found, '\0', len);
                memcpy_s(pdb_found, len, pdb_name.c_str(), pdb_name.length());
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
    wchar_t ORIG_PDB_FILE[MAX_PATH];
    memset(ORIG_PDB_FILE, 0, sizeof(ORIG_PDB_FILE));
    bool result = pdb_replace(fname, pdbname, ORIG_PDB_FILE, sizeof(ORIG_PDB_FILE));
    result &= static_cast<bool>(CopyFile(ORIG_PDB_FILE, pdbname.c_str(), 0));
    return result;
}
