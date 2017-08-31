#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <libgen.h>
#include <sys/stat.h>

#include "mincrypt/sha.h"
#include "bootimg.h"

typedef unsigned char byte;

int read_padding(FILE* f, unsigned itemsize, int pagesize)
{
    byte* buf = (byte*)malloc(sizeof(byte) * pagesize);
    unsigned pagemask = pagesize - 1;
    unsigned count;

    if((itemsize & pagemask) == 0) {
        free(buf);
        return 0;
    }

    count = pagesize - (itemsize & pagemask);

    if(fread(buf, count, 1, f)){};
    free(buf);
    return count;
}

void write_string_to_file(const char* file, const char* string)
{
    FILE* f = fopen(file, "w");
    fwrite(string, strlen(string), 1, f);
    fwrite("\n", 1, 1, f);
    fclose(f);
}

const char *detect_hash_type(const struct boot_img_hdr *hdr)
{
    /*
     * This isn't a sophisticated or 100% reliable method to detect the hash
     * type but it's probably good enough.
     *
     * sha256 is expected to have no zeroes in the id array
     * sha1 is expected to have zeroes in id[5], id[6] and id[7]
     * Zeroes anywhere else probably indicates neither.
     */
    const uint32_t *id = hdr->id;
    if (id[0] != 0 && id[1] != 0 && id[2] != 0 && id[3] != 0 &&
        id[4] != 0 && id[5] != 0 && id[6] != 0 && id[7] != 0)
        return "sha256";
    else if (id[0] != 0 && id[1] != 0 && id[2] != 0 && id[3] != 0 &&
        id[4] != 0 && id[5] == 0 && id[6] == 0 && id[7] == 0)
        return "sha1";
    else
        return "unknown";
}

int usage() {
    printf("usage: unpackbootimg\n");
    printf("\t-i|--input boot.img\n");
    printf("\t[ -o|--output output_directory]\n");
    printf("\t[ -p|--pagesize <size-in-hexadecimal> ]\n");
    return 0;
}

int main(int argc, char** argv)
{
    char tmp[PATH_MAX];
    char* directory = "./";
    char* filename = NULL;
    int pagesize = 0;
    int base = 0;

    argc--;
    argv++;
    while(argc > 0){
        char *arg = argv[0];
        char *val = argv[1];
        argc -= 2;
        argv += 2;
        if(!strcmp(arg, "--input") || !strcmp(arg, "-i")) {
            filename = val;
        } else if(!strcmp(arg, "--output") || !strcmp(arg, "-o")) {
            directory = val;
        } else if(!strcmp(arg, "--pagesize") || !strcmp(arg, "-p")) {
            pagesize = strtoul(val, 0, 16);
        } else {
            return usage();
        }
    }

    if (filename == NULL) {
        return usage();
    }

    int total_read = 0;
    FILE* f = fopen(filename, "rb");
    boot_img_hdr header;

    //printf("Reading header...\n");
    int i;
    int seeklimit = 65536;
    for (i = 0; i <= seeklimit; i++) {
        fseek(f, i, SEEK_SET);
        if(fread(tmp, BOOT_MAGIC_SIZE, 1, f)){};
        if (memcmp(tmp, BOOT_MAGIC, BOOT_MAGIC_SIZE) == 0)
            break;
    }
    total_read = i;
    if (i > seeklimit) {
        printf("Android boot magic not found.\n");
        return 1;
    }
    fseek(f, i, SEEK_SET);
    if (i > 0) {
        printf("Android magic found at: %d\n", i);
    }

    if(fread(&header, sizeof(header), 1, f)){};
    base = header.kernel_addr - 0x00008000;
    printf("BOARD_KERNEL_CMDLINE %s\n", header.cmdline);
    printf("BOARD_KERNEL_BASE %08x\n", base);
    printf("BOARD_NAME %s\n", header.name);
    printf("BOARD_PAGE_SIZE %d\n", header.page_size);
    printf("BOARD_HASH_TYPE %s\n", detect_hash_type(&header));
    printf("BOARD_KERNEL_OFFSET %08x\n", header.kernel_addr - base);
    printf("BOARD_RAMDISK_OFFSET %08x\n", header.ramdisk_addr - base);
    printf("BOARD_SECOND_OFFSET %08x\n", header.second_addr - base);
    printf("BOARD_TAGS_OFFSET %08x\n", header.tags_addr - base);
    int a, b, c, y, m;
    a = b = c = y = m = 0;
    if (header.os_version != 0) {
        int os_version,os_patch_level;
        os_version = header.os_version >> 11;
        os_patch_level = header.os_version&0x7ff;

        a = (os_version >> 14)&0x7f;
        b = (os_version >> 7)&0x7f;
        c = os_version&0x7f;

        y = (os_patch_level >> 4) + 2000;
        m = os_patch_level&0xf;

        if((a < 128) && (b < 128) && (c < 128) && (y >= 2000) && (y < 2128) && (m > 0) && (m <= 12)) {
            printf("BOARD_OS_VERSION %d.%d.%d\n", a, b, c);
            printf("BOARD_OS_PATCH_LEVEL %d-%02d\n", y, m);
        } else {
            header.os_version = 0;
        }
    }
    if (header.dt_size != 0) {
        printf("BOARD_DT_SIZE %d\n", header.dt_size);
    }

    if (pagesize == 0) {
        pagesize = header.page_size;
    }

    //printf("cmdline...\n");
    sprintf(tmp, "%s/%s", directory, basename(filename));
    strcat(tmp, "-cmdline");
    write_string_to_file(tmp, (char *)header.cmdline);

    //printf("board...\n");
    sprintf(tmp, "%s/%s", directory, basename(filename));
    strcat(tmp, "-board");
    write_string_to_file(tmp, (char *)header.name);

    //printf("base...\n");
    sprintf(tmp, "%s/%s", directory, basename(filename));
    strcat(tmp, "-base");
    char basetmp[200];
    sprintf(basetmp, "%08x", base);
    write_string_to_file(tmp, basetmp);

    //printf("pagesize...\n");
    sprintf(tmp, "%s/%s", directory, basename(filename));
    strcat(tmp, "-pagesize");
    char pagesizetmp[200];
    sprintf(pagesizetmp, "%d", header.page_size);
    write_string_to_file(tmp, pagesizetmp);

    //printf("kerneloff...\n");
    sprintf(tmp, "%s/%s", directory, basename(filename));
    strcat(tmp, "-kerneloff");
    char kernelofftmp[200];
    sprintf(kernelofftmp, "%08x", header.kernel_addr - base);
    write_string_to_file(tmp, kernelofftmp);

    //printf("ramdiskoff...\n");
    sprintf(tmp, "%s/%s", directory, basename(filename));
    strcat(tmp, "-ramdiskoff");
    char ramdiskofftmp[200];
    sprintf(ramdiskofftmp, "%08x", header.ramdisk_addr - base);
    write_string_to_file(tmp, ramdiskofftmp);

    //printf("secondoff...\n");
    sprintf(tmp, "%s/%s", directory, basename(filename));
    strcat(tmp, "-secondoff");
    char secondofftmp[200];
    sprintf(secondofftmp, "%08x", header.second_addr - base);
    write_string_to_file(tmp, secondofftmp);

    //printf("tagsoff...\n");
    sprintf(tmp, "%s/%s", directory, basename(filename));
    strcat(tmp, "-tagsoff");
    char tagsofftmp[200];
    sprintf(tagsofftmp, "%08x", header.tags_addr - base);
    write_string_to_file(tmp, tagsofftmp);

    if (header.os_version != 0) {
        //printf("os_version...\n");
        sprintf(tmp, "%s/%s", directory, basename(filename));
        strcat(tmp, "-osversion");
        char osvertmp[200];
        sprintf(osvertmp, "%d.%d.%d", a, b, c);
        write_string_to_file(tmp, osvertmp);

        //printf("os_patch_level...\n");
        sprintf(tmp, "%s/%s", directory, basename(filename));
        strcat(tmp, "-oslevel");
        char oslvltmp[200];
        sprintf(oslvltmp, "%d-%02d", y, m);
        write_string_to_file(tmp, oslvltmp);
    }

    //printf("hash...\n");
    sprintf(tmp, "%s/%s", directory, basename(filename));
    strcat(tmp, "-hash");
    const char *hashtype = detect_hash_type(&header);
    write_string_to_file(tmp, hashtype);

    total_read += sizeof(header);
    //printf("total read: %d\n", total_read);
    total_read += read_padding(f, sizeof(header), pagesize);

    sprintf(tmp, "%s/%s", directory, basename(filename));
    strcat(tmp, "-zImage");
    FILE *k = fopen(tmp, "wb");
    byte* kernel = (byte*)malloc(header.kernel_size);
    //printf("Reading kernel...\n");
    if(fread(kernel, header.kernel_size, 1, f)){};
    total_read += header.kernel_size;
    fwrite(kernel, header.kernel_size, 1, k);
    fclose(k);

    //printf("total read: %d\n", header.kernel_size);
    total_read += read_padding(f, header.kernel_size, pagesize);

    sprintf(tmp, "%s/%s", directory, basename(filename));
    strcat(tmp, "-ramdisk.gz");
    FILE *r = fopen(tmp, "wb");
    byte* ramdisk = (byte*)malloc(header.ramdisk_size);
    //printf("Reading ramdisk...\n");
    if(fread(ramdisk, header.ramdisk_size, 1, f)){};
    total_read += header.ramdisk_size;
    fwrite(ramdisk, header.ramdisk_size, 1, r);
    fclose(r);

    //printf("total read: %d\n", header.ramdisk_size);
    total_read += read_padding(f, header.ramdisk_size, pagesize);

    /*
     * Even though the second_size is 0, some vendors may place a signature
     * image there. In such a case read to EOF and try to calculate the
     * size based on termination with a null, then fix it in header.
     */
    if (header.second_size == 0) {
        struct stat st;
        stat(filename, &st);
        int second_test_size = st.st_size - total_read;
        if (second_test_size > header.dt_size) {
            byte* second_test = (byte*)malloc(second_test_size);
            if(fread(second_test, second_test_size, 1, f)){};
            u_int16_t *sbuf = (u_int16_t*)second_test;
            if (sbuf[0]) {
                int second_size = 0;
                while (sbuf[0]) {
                    second_size += 2;
                    sbuf++;
                }
                if ((second_size >= 1024) && (second_size < 2048))
                    header.second_size = second_size;
            }
            fseek(f, total_read, SEEK_SET);
        }
    }
    if (header.second_size != 0) {
        sprintf(tmp, "%s/%s", directory, basename(filename));
        strcat(tmp, "-second");
        FILE *s = fopen(tmp, "wb");
        byte* second = (byte*)malloc(header.second_size);
        //printf("Reading second...\n");
        if(fread(second, header.second_size, 1, f)){};
        total_read += header.second_size;
        fwrite(second, header.second_size, 1, s);
        fclose(s);
    }

    //printf("total read: %d\n", header.second_size);
    total_read += read_padding(f, header.second_size, pagesize);

    if (header.dt_size != 0) {
        sprintf(tmp, "%s/%s", directory, basename(filename));
        strcat(tmp, "-dtb");
        FILE *d = fopen(tmp, "wb");
        byte* dtb = (byte*)malloc(header.dt_size);
        //printf("Reading dtb...\n");
        if(fread(dtb, header.dt_size, 1, f)){};
        total_read += header.dt_size;
        fwrite(dtb, header.dt_size, 1, d);
        fclose(d);
    }

    fclose(f);

    //printf("Total Read: %d\n", total_read);
    return 0;
}
