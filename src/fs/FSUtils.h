#ifndef __FS_UTILS_H_
#define __FS_UTILS_H_

#include <vector>
#include <wut_types.h>

class FSUtils {
public:
    static int32_t LoadFileToMem(const char *filepath, uint8_t **inbuffer, uint32_t *size);

    //! todo: C++ class
    static int32_t CreateSubfolder(const char *fullpath);

    static int32_t CheckFile(const char *filepath);

    static int32_t saveBufferToFile(const char *path, void *buffer, uint32_t size);

    //! Reads a whole file through FSOpenFile/FSReadFile only (no stat, no seek). Needed for
    //! the virtual files of homebrew titles, which the Aroma homebrew_on_menu plugin serves
    //! by hooking exactly those calls. path is an FS path like "/vol/storage_mlc01/...".
    static bool LoadFileRawFS(const char *path, std::vector<uint8_t> &out);
};

#endif // __FS_UTILS_H_
