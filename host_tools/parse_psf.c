// 解析 psf 字体文件，生成 C 代码，以便嵌入内核
// psf 是点阵字体，适合用于 framebuffer

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


const uint8_t PSF1_MAGIC[2] = { 0x36, 0x04 };
const uint8_t PSF2_MAGIC[4] = { 0x72, 0xb5, 0x4a, 0x86 };


typedef struct psf1_header {
    uint16_t magic;
    uint8_t  font_mode;
    uint8_t  char_size;
} psf1_header_t;

#define PSF1_MODE_512    1 // 有 512 个图元，否则是 256 个
#define PSF1_MODE_HASTAB 2 // 文件包含 unicode table
#define PSF1_MODE_SEQ    4 // 含义同上


typedef struct psf2_header {
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;
    uint32_t font_flags;
    uint32_t length;
    uint32_t char_size;
    uint32_t height;
    uint32_t width;
} psf2_header_t;

#define PSF2_HAS_UNICODE_TABLE 1 // 文件包含 unicode table


void parse_psf1(uint8_t *data, long size) {
    psf1_header_t *hdr = (psf1_header_t *)data;
    uint8_t *fonts = data + sizeof(psf1_header_t);

    printf("static const uint8_t glyph_data[] = {\n");
    for (int i = 0; i < 256; ++i) {
        printf("    0x%02x", *fonts++);
        for (int j = 1; j < hdr->char_size; ++j) {
            printf(", 0x%02x", *fonts++);
        }
        printf(",\n");
    }
    printf("};\n");

    printf("font_data_t font = {\n");
    printf("    .height = %d,\n", hdr->char_size);
    printf("    .width = %d,\n", 8);
    printf("    .char_step = %d,\n", 1);
    printf("    .char_size = %d,\n", hdr->char_size);
    printf("    .data   = glyph_data,\n");
    printf("};\n");
}



void parse_psf2(uint8_t *data, long size) {
    psf2_header_t *hdr = (psf2_header_t *)data;
    uint8_t *font = data + hdr->header_size;
    int char_step = (hdr->width + 7) >> 3;

    printf("static const uint8_t glyph_data[] = {\n");
    for (int i = 0; i < 256; ++i) {
        printf("    0x%02x", *font++);
        for (int j = 1; j < hdr->char_size; ++j) {
            printf(", 0x%02x", *font++);
        }
        printf(",\n");
    }
    printf("};\n");

    printf("font_data_t font = {\n");
    printf("    .height = %d,\n", hdr->height);
    printf("    .width = %d,\n", hdr->width);
    printf("    .char_step = %d,\n", char_step);
    printf("    .char_size = %d,\n", hdr->char_size);
    printf("    .data   = glyph_data,\n");
    printf("};\n");
}


int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "no input file\n");
        return 1;
    }

    FILE *f = fopen(argv[1], "rb");
    if (NULL == f) {
        fprintf(stderr, "cannot open file %s\n", argv[1]);
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    uint8_t *data = malloc(size);
    rewind(f);
    fread(data, 1, size, f);
    fclose(f);

    if (0 == memcmp(data, PSF1_MAGIC, 2)) {
        parse_psf1(data, size);
    } else if (0 == memcmp(data, PSF2_MAGIC, 4)) {
        parse_psf2(data, size);
    }

    free(data);
    return 0;
}
