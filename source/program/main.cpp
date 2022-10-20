#include "lib.hpp"
#include "lib/util/modules.hpp"
#include <nn.hpp>
#include <cstring>
#include "cJSON.h"

typedef struct
{
    u64 crc;
    char const *replacement;
} stringList;

stringList *g_stringList = NULL;
size_t g_stringListSize = 0;

/* Function ptr to dread's crc function. */
u64 (*crc64)(char const *str, u64 size) = NULL;

/* Takes in a pointer to string and if found in the list, is replaced with the desired string. */
void replaceString(const char **str)
{
    /* Hash the string for quicker comparison. */
    u64 crc = crc64(*str, strlen(*str));

    /* Attempt to find matching hash in our list. */
    for(size_t i = 0; i < g_stringListSize; i++)
    {
        /* If the string matches, replace the string. */
        if(crc == g_stringList[i].crc)
            *str = g_stringList[i].replacement;
    }
}

HOOK_DEFINE_TRAMPOLINE(ForceRomfs) {
    /* Define the callback for when the function is called. Don't forget to make it static and name it Callback. */
    static void Callback(void *CFilePathStrIdOut, const char *path, u8 flags) {

        /* Just in case the path is NULL, pass it down to the real implementation, since we don't support replacing NULL paths anyway. */
        if(path == NULL)
        {
            Orig(CFilePathStrIdOut, path, flags);
            return;
        }

        /* Replace string if we have it in our list before passing to the real implementation. */
        replaceString(&path);
        Orig(CFilePathStrIdOut, path, flags);
    }
};


/* Allocates a buffer and reads from the specified file. */
void *openAndReadFile(const char *path)
{
    long int size;
    nn::fs::FileHandle fileHandle;
    nn::fs::OpenFile(&fileHandle, path, nn::fs::OpenMode_Read);
    nn::fs::GetFileSize(&size, fileHandle);
    u8 *fileBuf = (u8 *)malloc(size + 1);
    nn::fs::ReadFile(fileHandle, 0, fileBuf, size);
    nn::fs::CloseFile(fileHandle);
    fileBuf[size] = '\0';
    return fileBuf;
}

void populateStringReplacementList()
{
    /* Read contents of replacements.json in romfs. Allocates a heap buffer for the file contents and returns it. */
    char *fileBuf = (char *)openAndReadFile("rom:/replacements.json");

    /* Parse json from file buffer. */
    cJSON *json = cJSON_Parse(fileBuf);
    if(json == NULL)
        return;

    /* Get the "replacements" object within the json. */
    const cJSON *replacementList = cJSON_GetObjectItem(json, "replacements");

    /* Get number of elements in the array, for later use. */
    g_stringListSize = cJSON_GetArraySize(replacementList);

    /* Allocate space for the list of strings to replace. */
    g_stringList = (stringList *)malloc(g_stringListSize * sizeof(stringList));

    size_t i = 0;
    const cJSON *itemObject;

    /* Iterate over array contents and extract strings. */
    cJSON_ArrayForEach(itemObject, replacementList)
    {
        /* Extract the strings using the relevant method and add them to the list. */
        if(cJSON_IsString(itemObject))
        {
            char *fileStr = cJSON_GetStringValue(itemObject);
            char *replacementFileStr = (char *)malloc(strlen(fileStr) + strlen("rom:/") + 1);
            strcpy(replacementFileStr, "rom:/");
            replacementFileStr = strcat(replacementFileStr, fileStr);
            g_stringList[i].crc = crc64(fileStr, strlen(fileStr));
            g_stringList[i].replacement = replacementFileStr;
        } 
        else if(cJSON_IsObject(itemObject))
        {
            char const *str = cJSON_GetItemName(itemObject);
            g_stringList[i].crc = crc64(str, strlen(str));
            g_stringList[i].replacement = cJSON_GetStringValue(itemObject->child);
        }
        i++;
    }

    /* Free the buffer allocated by openAndReadFile, since we're done with it. */
    free(fileBuf);
}

/* Hook romfs mounting. Pass the arguments down to the real implementation so romfs is mounted as normal. */
/* Once romfs is mounted, we can read files from it in order to populate our string replacement list. */
HOOK_DEFINE_TRAMPOLINE(RomMounted) {
    static Result Callback(char const *path, void *romCache, unsigned long cacheSize) {
        Result res = Orig(path, romCache, cacheSize);
        populateStringReplacementList();
        return res;
    }
};


typedef struct
{
    uintptr_t crc64;
    uintptr_t CFilePathStrIdCtor;
} functionOffsets;

/* Handle version differences */
void getVersionOffsets(functionOffsets *offsets)
{
    nn::oe::DisplayVersion dispVer;
    nn::oe::GetDisplayVersion(&dispVer);

    if(strcmp(dispVer.displayVersion, "2.1.0") == 0)
    {
        offsets->crc64 = 0x1570;
        offsets->CFilePathStrIdCtor = 0x166C8;
    } 
    else /* 1.0.0 - 2.0.0 */
    {
        offsets->crc64 = 0x1570;
        offsets->CFilePathStrIdCtor = 0x16624;
    }
}

extern "C" void exl_main(void* x0, void* x1)
{
    functionOffsets offsets;
    /* Setup hooking enviroment. */
    envSetOwnProcessHandle(exl::util::proc_handle::Get());
    exl::hook::Initialize();

    getVersionOffsets(&offsets);

    /* Install the hook at the provided function pointer. Function type is checked against the callback function. */
    /* Hook functions we care about */
    ForceRomfs::InstallAtPtr(offsets.CFilePathStrIdCtor);
    RomMounted::InstallAtFuncPtr(nn::fs::MountRom);

    /* Alternative install funcs: */
    /* InstallAtPtr takes an absolute address as a uintptr_t. */
    /* InstallAtOffset takes an offset into the main module. */

    /* Get the address of dread's crc64 function */
    crc64 = (u64 (*)(char const *, u64))exl::util::modules::GetTargetOffset(offsets.crc64);
}

extern "C" NORETURN void exl_exception_entry()
{
    /* TODO: exception handling */
    EXL_ABORT(0x420);
}