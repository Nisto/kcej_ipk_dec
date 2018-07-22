#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>

#ifdef _WIN32 // Windows
#include <Windows.h>
#else // *nix
#include <sys/stat.h>
#endif // _WIN32

uint32_t get_u32_le(uint8_t *mem)
{
    return (mem[3] << 24) | (mem[2] << 16) | (mem[1] << 8) | (mem[0]);
}

void put_u32_le(uint8_t *mem, uint32_t x)
{
    mem[0] = (uint8_t) (x       & 0xFF);
    mem[1] = (uint8_t) (x >>  8 & 0xFF);
    mem[2] = (uint8_t) (x >> 16 & 0xFF);
    mem[3] = (uint8_t) (x >> 24 & 0xFF);
}

/* substitute basename with replacement string */
char *subname(char *path, const char *rep)
{
    int i, s = 0;
    char *out;

    for (i = 0; path[i]; i++) {
#ifdef _WIN32
        if (path[i] == '/' || path[i] == '\\') {
#else
        if (path[i] == '/') {
#endif
            s = i + 1;
        }
    }

    out = malloc(s + strlen(rep) + 1);

    if (!out) return NULL;

    memcpy(out, path, s);

    strcpy(out + s, rep);

    return out;
}

int isfile(char *path)
{
#if _WIN32
    DWORD dwAttrib = GetFileAttributes(path);
    if (dwAttrib != INVALID_FILE_ATTRIBUTES) {
        if (!(dwAttrib & FILE_ATTRIBUTE_DIRECTORY)) {
            return 1;
        }
    }
    return 0;
#else
    struct stat s;
    if (stat(path, &s) == 0) {
        if(s.st_mode & S_IFREG) {
            return 1;
        }
    }
    return 0;
#endif
}

uint64_t filesize(char *path)
{
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA attr;
    GetFileAttributesExA((LPCSTR)path, GetFileExInfoStandard, &attr);
    return (uint64_t) (((uint64_t)attr.nFileSizeHigh << 32) | (uint64_t)attr.nFileSizeLow);
#else
    struct stat64 st;
    lstat64(path, &st);
    return (uint64_t) st.st_size;
#endif
}

char *abspath(char *path)
{
    char *buf = malloc(1024);
    if (!buf) return NULL;
#ifdef _WIN32
    GetFullPathName((LPCSTR)path, 1024, (LPSTR)buf, NULL);
#else
    realpath(path, buf);
#endif
    return buf;
}

#define INIT_KEY UINT32_C(0x7E8A6B4C)

uint32_t dec_key = INIT_KEY;

void decode(uint8_t *buf, uint32_t todo)
{
    uint8_t *ptr = buf;

    while (todo > 0) {
        put_u32_le(ptr, get_u32_le(ptr) ^ dec_key);
        dec_key += UINT32_C(0xA84E6B2E);
        todo -= 4;
        ptr += 4;
    }
}

int main(int argc, char *argv[])
{
    FILE *ipk = NULL;
    FILE *irx = NULL;
    char *ipkpath = NULL;
    char *irxpath = NULL;
    uint64_t ipksize = 0;
    uint32_t irxsize = 0;
    uint8_t *irxbuf = NULL;
    uint32_t irxbufsize = 0;
    uint8_t header[16] = {0};
    uint32_t nameoff = 0;

    if (argc != 2) {
        printf("Usage: %s module.ipk\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    ipkpath = abspath(argv[1]);

    if (!isfile(ipkpath)) {
        printf("ERROR: Invalid path to module.ipk\n");
        exit(EXIT_FAILURE);
    }

    ipk = fopen(ipkpath, "rb");

    if (!ipk) {
        printf("ERROR: Could not open module.ipk\n");
        exit(EXIT_FAILURE);
    }

    ipksize = filesize(ipkpath);

    while (ftell(ipk) + sizeof(header) < ipksize) {
        dec_key = INIT_KEY;

        if (sizeof(header) != fread(header, sizeof(uint8_t), sizeof(header), ipk)) {
            printf("ERROR: Unable to read header at 0x%X\n", ftell(ipk));
            exit(EXIT_FAILURE);
        }

        decode(header, sizeof(header));

        irxsize = get_u32_le(&header[0x00]);

        nameoff = get_u32_le(&header[0x04]);

        if (irxbufsize == 0) {
            irxbuf = malloc(irxsize);
            irxbufsize = irxsize;
        } else if (irxsize > irxbufsize) {
            irxbuf = realloc(irxbuf, irxsize);
            irxbufsize = irxsize;
        }

        if (!irxbuf) {
            printf("ERROR: Could not allocate memory for file at 0x%X\n", ftell(ipk));
            exit(EXIT_FAILURE);
        }

        if (irxsize != fread(irxbuf, sizeof(uint8_t), irxsize, ipk)) {
            printf("ERROR: Could not read data at 0x%X\n", ftell(ipk));
            exit(EXIT_FAILURE);
        }

        decode(irxbuf, irxsize);

        irxpath = subname(ipkpath, (char*)(irxbuf+nameoff));

        if (!irxpath) {
            printf("ERROR: Could not build output path\n");
            exit(EXIT_FAILURE);
        }

        irx = fopen(irxpath, "wb");

        if (!irx) {
            printf("ERROR: Could not open output file\n");
            exit(EXIT_FAILURE);
        }

        if (nameoff != fwrite(irxbuf, sizeof(uint8_t), nameoff, irx)) {
            printf("ERROR: Could not write output data\n");
            exit(EXIT_FAILURE);
        }

        fclose(irx);
        free(irxpath);
    }

    free(irxbuf);
    free(ipkpath);
    fclose(ipk);

    return 0;
}
