#include "fs/FSUtils.h"
#include <coreinit/filesystem.h>
#include "fs/CFile.hpp"
#include "utils/logger.h"
#include <fcntl.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int32_t FSUtils::LoadFileToMem(const char *filepath, uint8_t **inbuffer, uint32_t *size) {
    //! always initialze input
    *inbuffer = nullptr;
    if (size)
        *size = 0;

    int32_t iFd = open(filepath, O_RDONLY);
    if (iFd < 0)
        return -1;

    off_t end = lseek(iFd, 0, SEEK_END);
    lseek(iFd, 0, SEEK_SET);
    if (end <= 0 || end > 64 * 1024 * 1024) {
        close(iFd);
        return -2;
    }
    uint32_t filesize = (uint32_t) end;

    //! The FS reads straight into an aligned buffer by DMA in 0x40 byte units, so the last
    //! read can write up to 63 bytes past the requested size. A buffer of the exact file
    //! size let that overwrite the neighbouring allocation (seen as corrupted icon
    //! textures). Align start and size to 0x40.
    uint32_t allocSize = (filesize + 0x3F) & ~0x3F;
    uint8_t *buffer    = (uint8_t *) memalign(0x40, allocSize > 0 ? allocSize : 0x40);
    if (buffer == nullptr) {
        close(iFd);
        return -2;
    }

    uint32_t blocksize = 0x4000;
    uint32_t done      = 0;
    int32_t readBytes  = 0;

    while (done < filesize) {
        if (done + blocksize > filesize) {
            blocksize = filesize - done;
        }
        readBytes = read(iFd, buffer + done, blocksize);
        if (readBytes <= 0)
            break;
        done += readBytes;
    }

    close(iFd);

    if (done != filesize) {
        free(buffer);
        buffer = nullptr;
        return -3;
    }

    *inbuffer = buffer;

    //! sign is optional input
    if (size) {
        *size = filesize;
    }

    return filesize;
}

int32_t FSUtils::CheckFile(const char *filepath) {
    if (!filepath)
        return 0;

    struct stat filestat;

    char dirnoslash[strlen(filepath) + 2];
    snprintf(dirnoslash, sizeof(dirnoslash), "%s", filepath);

    while (dirnoslash[strlen(dirnoslash) - 1] == '/')
        dirnoslash[strlen(dirnoslash) - 1] = '\0';

    char *notRoot = strrchr(dirnoslash, '/');
    if (!notRoot) {
        strcat(dirnoslash, "/");
    }

    if (stat(dirnoslash, &filestat) == 0)
        return 1;

    return 0;
}

int32_t FSUtils::CreateSubfolder(const char *fullpath) {
    if (!fullpath)
        return 0;

    int32_t result = 0;

    char dirnoslash[strlen(fullpath) + 1];
    strcpy(dirnoslash, fullpath);

    int32_t pos = strlen(dirnoslash) - 1;
    while (dirnoslash[pos] == '/') {
        dirnoslash[pos] = '\0';
        pos--;
    }

    if (CheckFile(dirnoslash)) {
        return 1;
    } else {
        char parentpath[strlen(dirnoslash) + 2];
        strcpy(parentpath, dirnoslash);
        char *ptr = strrchr(parentpath, '/');

        if (!ptr) {
            //!Device root directory (must be with '/')
            strcat(parentpath, "/");
            struct stat filestat;
            if (stat(parentpath, &filestat) == 0)
                return 1;

            return 0;
        }

        ptr++;
        ptr[0] = '\0';

        result = CreateSubfolder(parentpath);
    }

    if (!result)
        return 0;

    if (mkdir(dirnoslash, 0777) == -1) {
        return 0;
    }

    return 1;
}

int32_t FSUtils::saveBufferToFile(const char *path, void *buffer, uint32_t size) {
    CFile file(path, CFile::WriteOnly);
    if (!file.isOpen()) {
        DEBUG_FUNCTION_LINE("Failed to open %s", path);
        return 0;
    }
    int32_t written = file.write((const uint8_t *) buffer, size);
    file.close();
    return written;
}

bool FSUtils::LoadFileRawFS(const char *path, std::vector<uint8_t> &out) {
    out.clear();
    auto *client = (FSClient *) memalign(0x40, sizeof(FSClient));
    auto *block  = (FSCmdBlock *) memalign(0x40, sizeof(FSCmdBlock));
    constexpr uint32_t CHUNK = 0x4000;
    auto *chunk  = (uint8_t *) memalign(0x40, CHUNK);
    bool result  = false;
    if (client && block && chunk) {
        FSInit();
        if (FSAddClient(client, FS_ERROR_FLAG_ALL) == FS_STATUS_OK) {
            FSInitCmdBlock(block);
            FSFileHandle handle = 0;
            if (FSOpenFile(client, block, path, "r", &handle, FS_ERROR_FLAG_ALL) == FS_STATUS_OK) {
                result = true;
                while (true) {
                    FSStatus read = FSReadFile(client, block, chunk, 1, CHUNK, handle, 0, FS_ERROR_FLAG_ALL);
                    if (read < 0) {
                        result = false;
                        break;
                    }
                    if (read == 0) {
                        break;
                    }
                    out.insert(out.end(), chunk, chunk + read);
                    if (out.size() > 16 * 1024 * 1024) { // no icon or splash is that big
                        result = false;
                        break;
                    }
                }
                FSCloseFile(client, block, handle, FS_ERROR_FLAG_ALL);
            }
            FSDelClient(client, FS_ERROR_FLAG_ALL);
        }
    }
    free(chunk);
    free(block);
    free(client);
    if (!result) {
        out.clear();
    }
    return result;
}
