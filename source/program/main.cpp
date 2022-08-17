#include "lib.hpp"
#include <nn.hpp>
#include <cstring>
#include "cJSON.h"
#include "remote_api.hpp"
#include "lua-5.1.5/src/lua.hpp"

typedef struct
{
    u64 crc;
    char const *replacement;
} stringList;

stringList *g_stringList = NULL;
size_t g_stringListSize = 0;

/* Function ptr to dread's crc function. */
u64 (*crc64)(char const *str, u64 size) = NULL;

/* The main executable's pcall, so we get proper error handling. */
int (*exefs_lua_pcall) (lua_State *L, int nargs, int nresults, int errfunc) = NULL;

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

/* Hook the function that handles conversion of a raw path to a CFilePathStrId, allowing the paths to be redirected. */
MAKE_HOOK_T(void, forceRomfs, (void *CFilePathStrIdOut, const char *path, u8 flags),

    /* Just in case the path is NULL, pass it down to the real implementation, since we don't support replacing NULL paths anyway. */
    if(path == NULL)
    {
        impl(CFilePathStrIdOut, path, flags);
        return;
    }

    /* Replace string if we have it in our list before passing to the real implementation. */
    replaceString(&path);
    impl(CFilePathStrIdOut, path, flags);
    return;
);

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
MAKE_HOOK_T(Result, romMounted, (char const *path, void *romCache, unsigned long cacheSize),
    Result res = impl(path, romCache, cacheSize);
    populateStringReplacementList();
    return res;
);

void multiworld_schedule_update(lua_State* L) {
    lua_getglobal(L, "Game");
    lua_getfield(L, -1, "AddGUISF");

    lua_pushinteger(L, 0);
    lua_pushstring(L, "RemoteLua.Update");
    lua_pushstring(L, "");

    lua_call(L, 3, 0);
    lua_pop(L, 1);
}

int multiworld_init(lua_State* L) {
    RemoteApi::Init();
    multiworld_schedule_update(L);
    return 0;
}

int multiworld_update(lua_State* L) {
    RemoteApi::ProcessCommand([=](RemoteApi::CommandBuffer& buffer, size_t bufferLength) {
        size_t response = 0;

        bool output_success = false;
        char* output_start = buffer.data() + 4;
        size_t output_buffer_size = buffer.size() - 4;

        // +1; use lua's tostring so we properly convert all types
        lua_getglobal(L, "tostring");

        // +1
        int loadResult = luaL_loadbuffer(L, buffer.data(), bufferLength, "remote lua");

        if (loadResult == 0) {
            // -1, +1
            int pcallResult = exefs_lua_pcall(L, 0, 1, 0);
            // -2, +1
            lua_call(L, 1, 1);

            size_t resultSize;
            const char* luaResult = lua_tolstring(L, 1, &resultSize);
            
            if (pcallResult == 0) {
                // success! top string is the entire result
                output_success = true;
                memcpy(output_start, luaResult, std::min(output_buffer_size, resultSize));
                response = resultSize;
            } else {
                // error happened
                response = snprintf(output_start, output_buffer_size, "%s", luaResult);
            }
        } else {
            response = snprintf(output_start, output_buffer_size, "error parsing buffer: %d", loadResult);
        }
        buffer[0] = output_success;        
        buffer[1] = response & 0xff;
        buffer[2] = (response >> 8)  & 0xff;
        buffer[3] = (response >> 16) & 0xff;
        return response + 4;
    });

    // Register calling update again
    multiworld_schedule_update(L);
    return 0;
}

static const luaL_Reg multiworld_lib[] = {
  {"Init", multiworld_init},
  {"Update", multiworld_update},
  {NULL, NULL}  
};

/* Hook asdf */
MAKE_HOOK_T(void, luaRegisterGlobals, (lua_State* L),
    impl(L);

    lua_pushcfunction(L, luaopen_debug);
    lua_pushstring(L, "debug");
    lua_call(L, 1, 0);

    luaL_register(L, "RemoteLua", multiworld_lib);

    lua_pushinteger(L, RemoteApi::VERSION);
    lua_setfield(L, -2, "Version");

    lua_pushinteger(L, RemoteApi::BufferSize);
    lua_setfield(L, -2, "BufferSize");
);

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

    /* Hook functions we care about */
    INJECT_HOOK_T(offsets.CFilePathStrIdCtor, forceRomfs);
    INJECT_HOOK_T(nn::fs::MountRom, romMounted);
    INJECT_HOOK_T(0x106ce90, luaRegisterGlobals);

    /* Get the address of dread's crc64 function */
    crc64 = (u64 (*)(char const *, u64))exl::hook::GetTargetOffset(offsets.crc64);
    exefs_lua_pcall = (int (*) (lua_State *L, int nargs, int nresults, int errfunc)) exl::hook::GetTargetOffset(0x1061bc0);
}

extern "C" NORETURN void exl_exception_entry()
{
    /* TODO: exception handling */
    EXL_ABORT(0x420);
}