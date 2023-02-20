#include "lib.hpp"
#include "lib/util/modules.hpp"
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

void multiworld_schedule_update(lua_State* L) {
    lua_getglobal(L, "Game");
    lua_getfield(L, -1, "AddGUISF");

    lua_pushinteger(L, 0);
    lua_pushstring(L, "RemoteLua.Update");
    lua_pushstring(L, "");

    lua_call(L, 3, 0);
    lua_pop(L, 1);

    // set the RemoteLogHook variable in lua according to the client. logs are only sent over tcp socket iff true
    lua_pushboolean(L, RemoteApi::clientSubs.logging);
    lua_setglobal(L, "RemoteLogHook");
}

int multiworld_init(lua_State* L) {
    RemoteApi::Init();
    multiworld_schedule_update(L);
    return 0;
}

int multiworld_update(lua_State* L) {
    RemoteApi::ProcessCommand([=](RemoteApi::CommandBuffer& RecvBuffer, size_t RecvBufferLength, size_t &size) -> char* {
        size_t resultSize = 0;          // length of the lua string response (without \0)
        size_t packetHeaderLength = 6;  // length of the packet header
        bool outputSuccess = false;     // was the lua function call sucessfully
        char* sendBuffer;               // sendBuffer to store the result. this pointer is returned 
        size_t sendBufferSize;          // size of the dynamically allocated sendBuffer
        char* outputStart;              // where the user data starts in the buffer

        // +1; use lua's tostring so we properly convert all types
        lua_getglobal(L, "tostring");

        // +1
        int loadResult = luaL_loadbuffer(L, RecvBuffer.data() + 1, RecvBufferLength - 1, "remote lua");

        if (loadResult == 0) {
            // -1, +1 - call the code we just loaded
            int pcallResult = exefs_lua_pcall(L, 0, 1, 0);
            // -2, +1 - call tostring with the result of that
            lua_call(L, 1, 1);

            const char* luaResult = lua_tolstring(L, 1, &resultSize);
            
            if (pcallResult == 0) {
                // success! top string is the entire result
                sendBufferSize = resultSize + packetHeaderLength;
                sendBuffer = (char*) calloc(sendBufferSize, sizeof(char));
                if (sendBuffer == NULL) return NULL;
                outputStart = sendBuffer + 6;
                outputSuccess = true;
                memcpy(outputStart, luaResult, resultSize);
            } else {
                // error happened
                sendBufferSize = resultSize + packetHeaderLength;
                sendBuffer = (char*) calloc(sendBufferSize, sizeof(char));
                if (sendBuffer == NULL) return NULL;
                outputStart = sendBuffer + 6;
                // use +1 for the size because snprintf uses "n-1" bytes to keep space for \0 but we don't care about that in our send buffer
                int printSize = snprintf(outputStart, resultSize + 1, "%s", luaResult);
                // modify sendBufferSize to final amount which is used (don't care about the few extra bytes allocated, they will be freed eventually after send)
                sendBufferSize = sendBufferSize - resultSize + printSize;
            }
        } else {
            const char* errorString = "error parsing buffer: %d";
            // calculation is: len of errorString minus 2 for "%d" plus 11 for the maximum bytes an integer can take
            size_t maxStringLength = strlen(errorString) - 2 + 11;
            sendBufferSize = maxStringLength + packetHeaderLength;
            sendBuffer = (char*) calloc(sendBufferSize, sizeof(char));
            if (sendBuffer == NULL) return NULL;
            outputStart = sendBuffer + 6;
            // use +1 for the size because snprintf uses "n-1" bytes to keep space for \0 but we don't care about that in our send buffer
            int printSize = snprintf(outputStart, maxStringLength + 1, "error parsing buffer: %d", loadResult);
            // modify sendBufferSize to final amount which is used (don't care about the few extra bytes allocated, they will be freed eventually after send)
            sendBufferSize = sendBufferSize - resultSize + printSize;
        }
        sendBuffer[0] = PACKET_REMOTE_LUA_EXEC;
        sendBuffer[2] = outputSuccess;        
        sendBuffer[3] = resultSize & 0xff;
        sendBuffer[4] = (resultSize >> 8)  & 0xff;
        sendBuffer[5] = (resultSize >> 16) & 0xff;
        size = sendBufferSize;
        return sendBuffer;
    });

    // Register calling update again
    multiworld_schedule_update(L);
    return 0;
}

/* This function gets called by the lua to sent message to the client iff RemoteLogHook is true */
int gamelog_send(lua_State* L) {
    RemoteApi::SendLog([=](size_t &size) -> char* {
        size_t resultSize = 0;          // length of the lua string response (without \0)
        char* sendBuffer;               // sendBuffer to store the result. this pointer is returned 
        size_t sendBufferSize;          // size of the dynamically allocated sendBuffer
        char* outputStart;              // where the user data starts in the buffer // = sendBuffer + 6;
        size_t packetHeaderLength = 5;  // length of the packet header
       
        const char* luaResult = lua_tolstring(L, 1, &resultSize);

        sendBufferSize = resultSize + packetHeaderLength;
        sendBuffer = (char*) calloc(sendBufferSize, sizeof(char));
        if (sendBuffer == NULL) return NULL;
        outputStart = sendBuffer + packetHeaderLength;

        sendBuffer[0] = PACKET_LOG_MESSAGE;
        memcpy(&sendBuffer[1], &resultSize, sizeof(size_t));
        memcpy(outputStart, luaResult, resultSize);

        size = sendBufferSize;
        return sendBuffer;
    });
    return 1;
}


static const luaL_Reg multiworld_lib[] = {
  {"Init", multiworld_init},
  {"Update", multiworld_update},
  {"SendLog", gamelog_send},
  {NULL, NULL}  
};

/* Hook asdf */

HOOK_DEFINE_TRAMPOLINE(LuaRegisterGlobals) {
    static void Callback(lua_State* L) {
        Orig(L);
        
        lua_pushcfunction(L, luaopen_debug);
        lua_pushstring(L, "debug");
        lua_call(L, 1, 0);

        luaL_register(L, "RemoteLua", multiworld_lib);

        lua_pushinteger(L, RemoteApi::VERSION);
        lua_setfield(L, -2, "Version");

        lua_pushinteger(L, RemoteApi::BufferSize);
        lua_setfield(L, -2, "BufferSize");
    }
};

typedef struct
{
    ptrdiff_t crc64;
    ptrdiff_t CFilePathStrIdCtor;
    ptrdiff_t luaRegisterGlobals;
    ptrdiff_t lua_pcall;

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
        offsets->luaRegisterGlobals = 0x010aed50;
        offsets->lua_pcall = 0x010a3a80;
    } 
    else /* 1.0.0 - 2.0.0 */
    {
        offsets->crc64 = 0x1570;
        offsets->CFilePathStrIdCtor = 0x16624;
        offsets->luaRegisterGlobals = 0x106ce90;
        offsets->lua_pcall = 0x1061bc0;
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
    ForceRomfs::InstallAtOffset(offsets.CFilePathStrIdCtor);
    RomMounted::InstallAtFuncPtr(nn::fs::MountRom);
    LuaRegisterGlobals::InstallAtOffset(offsets.luaRegisterGlobals);

    /* Alternative install funcs: */
    /* InstallAtPtr takes an absolute address as a uintptr_t. */
    /* InstallAtOffset takes an offset into the main module. */

    /* Get the address of dread's crc64 function */
    crc64 = (u64 (*)(char const *, u64))exl::util::modules::GetTargetOffset(offsets.crc64);
    exefs_lua_pcall = (int (*) (lua_State *L, int nargs, int nresults, int errfunc)) exl::util::modules::GetTargetOffset(offsets.lua_pcall);
}

extern "C" NORETURN void exl_exception_entry()
{
    /* TODO: exception handling */
    EXL_ABORT(0x420);
}