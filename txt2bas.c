/*
 * txt2bas: text file to N88-BASIC converter
 *
 * Copyright (c) 2019 Hirokuni Yano
 *
 * Released under the MIT license.
 * see https://opensource.org/licenses/MIT
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

char line[1024];
uint8_t buff[0x10000];

void help(void)
{
    fprintf(stderr, "Usage: txt2bas file\n");
    exit(1);
}


int main(int _argc, char *_argv[])
{
    FILE *fp;
    char *p;
    int len;
    uint16_t lineno = 1000;
    uint32_t ptr = 0;
    uint32_t next;

    if (_argc != 2)
    {
        help();
    }

    while (fgets(line, sizeof(line), stdin) != NULL)
    {
        p = strchr(line, '\n');
        if(p) *p = '\0';
        p = strchr(line, '\r');
        if(p) *p = '\0';
        len = strlen(line) + 1;

        next = ptr + len + 8;
        buff[ptr++] = next;
        buff[ptr++] = next >> 8;
        buff[ptr++] = lineno;
        buff[ptr++] = lineno >> 8;
        buff[ptr++] = 0x3a;
        buff[ptr++] = 0x8f;
        buff[ptr++] = 0xe9;
        memcpy(&buff[ptr], line, len);
        ptr = next - 1;
        lineno += 10;
    }
    if (ptr < 1)
    {
        fprintf(stderr, "convert error\n");
        exit(1);
    }

    fp = fopen(_argv[1], "wb");
    if (fp == NULL)
    {
        fprintf(stderr, "Can't open '%s'\n", _argv[1]);
        exit(1);
    }
    ptr -= 1;
    fwrite(buff, sizeof(unsigned char), ptr, fp);
    fclose(fp);

    return 0;
}
