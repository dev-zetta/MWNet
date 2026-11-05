/* Read a single delivered library in a fresh, bounded controller process.
 * This helper is separate from the frozen game runtime and package payload. */
#define _GNU_SOURCE
#define _DARWIN_C_SOURCE 1
#include <dlfcn.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char loaded_path[PATH_MAX];

static void fail(const char *message)
{
    fprintf(stderr, "%s\n", message);
    exit(1);
}

static void *symbol(void *library, const char *name, const char *expected)
{
    Dl_info information;
    char actual[PATH_MAX], wanted[PATH_MAX];
    dlerror();
    void *value = dlsym(library, name);
    const char *error = dlerror();
    if (error != NULL || value == NULL)
        fail(error != NULL ? error : "Missing native symbol");
    if (dladdr(value, &information) == 0 || information.dli_fname == NULL
        || realpath(information.dli_fname, actual) == NULL
        || realpath(expected, wanted) == NULL || strcmp(actual, wanted) != 0)
        fail("Native symbol came from a different library provider");
    if (strlen(actual) >= sizeof(loaded_path))
        fail("Native provider path exceeds its bound");
    strcpy(loaded_path, actual);
    return value;
}

int main(int argc, char **argv)
{
    if (argc != 3)
        fail("Usage: native-library-probe LIBRARY KIND");
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (library == NULL)
        fail(dlerror());
    const char *kind = argv[2], *value = NULL;
    char version[80];
    const char *coverage = "version-api";
    if (strcmp(kind, "qt") == 0 || strcmp(kind, "sodium") == 0) {
        const char *(*function)(void) = (const char *(*)(void))symbol(
            library, strcmp(kind, "qt") == 0 ? "qVersion" : "sodium_version_string", argv[1]);
        value = function();
    } else if (strcmp(kind, "png") == 0) {
        const char *(*function)(const void *) = (const char *(*)(const void *))symbol(
            library, "png_get_libpng_ver", argv[1]);
        value = function(NULL);
    } else if (strcmp(kind, "fontconfig") == 0) {
        int (*function)(void) = (int (*)(void))symbol(library, "FcGetVersion", argv[1]);
        int encoded = function();
        if (encoded <= 0)
            fail("Invalid fontconfig version");
        snprintf(version, sizeof(version), "%d.%d.%d", encoded / 10000, (encoded / 100) % 100, encoded % 100);
        value = version;
    } else if (strcmp(kind, "intl") == 0) {
        int encoded = *(int *)symbol(library, "libintl_version", argv[1]);
        if (encoded < 0)
            fail("Invalid libintl version");
        snprintf(version, sizeof(version), "%d.%d.%d", (encoded >> 16) & 255, (encoded >> 8) & 255, encoded & 255);
        value = version;
    } else if (strcmp(kind, "freetype") == 0) {
        int (*initialize)(void **) = (int (*)(void **))symbol(library, "FT_Init_FreeType", argv[1]);
        void (*read_version)(void *, int *, int *, int *) =
            (void (*)(void *, int *, int *, int *))symbol(library, "FT_Library_Version", argv[1]);
        int (*finish)(void *) = (int (*)(void *))symbol(library, "FT_Done_FreeType", argv[1]);
        void *instance = NULL;
        int major = 0, minor = 0, patch = 0;
        if (initialize(&instance) != 0 || instance == NULL)
            fail("FT_Init_FreeType failed");
        read_version(instance, &major, &minor, &patch);
        if (finish(instance) != 0)
            fail("FT_Done_FreeType failed");
        snprintf(version, sizeof(version), "%d.%d.%d", major, minor, patch);
        value = version;
        coverage = "initialize-version-finish";
    } else if (strcmp(kind, "brotli") == 0) {
        uint32_t (*read_version)(void) = (uint32_t (*)(void))symbol(library, "BrotliDecoderVersion", argv[1]);
        int (*decode)(size_t, const uint8_t *, size_t *, uint8_t *) =
            (int (*)(size_t, const uint8_t *, size_t *, uint8_t *))symbol(library, "BrotliDecoderDecompress", argv[1]);
        uint32_t encoded = read_version();
        snprintf(version, sizeof(version), "%u.%u.%u", encoded >> 24, (encoded >> 12) & 4095, encoded & 4095);
        /* Self-generated plain text, encoded with Brotli 1.2.0, quality 5,
         * window 22, generic mode. No game or third-party fixture contents. */
        static const uint8_t compressed[] = {
            0x1b, 0xb7, 0x00, 0x00, 0x44, 0x4f, 0xd6, 0x5f,
            0xd2, 0x63, 0xa1, 0xea, 0xec, 0x99, 0x99, 0xa2,
            0x6e, 0xd1, 0xad, 0xaa, 0x13, 0x0b, 0xd7, 0xf1,
            0x77, 0xbc, 0xd5, 0x73, 0xb6, 0x53, 0x20
        };
        static const uint8_t expected[] =
            "MWNet dependency probe\nMWNet dependency probe\n"
            "MWNet dependency probe\nMWNet dependency probe\n"
            "MWNet dependency probe\nMWNet dependency probe\n"
            "MWNet dependency probe\nMWNet dependency probe\n";
        uint8_t output[sizeof(expected) - 1];
        size_t length = sizeof(output);
        if (decode(sizeof(compressed), compressed, &length, output) != 1
            || length != sizeof(expected) - 1 || memcmp(output, expected, length) != 0)
            fail("Brotli tiny-vector decode failed");
        value = version;
        coverage = "version-and-tiny-vector-decode";
    } else {
        fail("Unknown native probe kind");
    }
    if (value == NULL || strlen(value) > 200 || strchr(value, '\n') != NULL || strchr(value, '\r') != NULL)
        fail("Invalid native version value");
    printf("%s\n%s\n%s\n", loaded_path, value, coverage);
    if (dlclose(library) != 0)
        fail("Could not close native probe library");
    return 0;
}
