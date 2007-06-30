#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#define __STDC_FORMAT_MACROS 1
#include <inttypes.h>

#include "sg_lib.h"
#include "sg_cmds_basic.h"
#include "sg_cmds_extra.h"

/* A utility program for the Linux OS SCSI subsystem.
   *  Copyright (C) 2004-2007 D. Gilbert
   *  This program is free software; you can redistribute it and/or modify
   *  it under the terms of the GNU General Public License as published by
   *  the Free Software Foundation; either version 2, or (at your option)
   *  any later version.

   This program issues the SCSI command WRITE LONG to a given SCSI device. 
   It sends the command with the logical block address passed as the lba
   argument, and the transfer length set to the xfer_len argument. the
   buffer to be writen to the device filled with 0xff, this buffer includes
   the sector data and the ECC bytes.

   This code was contributed by Saeed Bishara
*/

static char * version_str = "1.14 20070419";


#define MAX_XFER_LEN 10000

/* #define SG_DEBUG */

#define ME "sg_write_long: "

#define EBUFF_SZ 256

static struct option long_options[] = {
        {"16", 0, 0, 'S'},
        {"cor_dis", 0, 0, 'c'},
        {"help", 0, 0, 'h'},
        {"in", 1, 0, 'i'},
        {"lba", 1, 0, 'l'},
        {"pblock", 0, 0, 'p'},
        {"verbose", 0, 0, 'v'},
        {"version", 0, 0, 'V'},
        {"wr_uncor", 0, 0, 'w'},
        {"xfer_len", 1, 0, 'x'},
        {0, 0, 0, 0},
};

static void usage()
{
  fprintf(stderr, "Usage: "
          "sg_write_long [--16] [--cor_dis] [--help] [--in=IF] "
          "[--lba=LBA]\n"
          "                     [--pblock] [--verbose] [--version] "
          "[--wr_uncor]\n"
          "                     [--xfer_len=BTL] DEVICE\n"
          "  where:\n"
          "    --16|-S              do WRITE LONG(16) (default: 10)\n"
          "    --cor_dis|-c         set correction disabled bit\n"
          "    --help|-h            print out usage message\n"
          "    --in=IF|-i IF        input from file called IF (default: "
          "use\n"
          "                         0xff bytes as fill)\n"
          "    --lba=LBA|-l LBA     logical block address "
          "(default: 0)\n"
          "    --pblock|-p          physical block (default: logical "
          "block)\n"
          "    --verbose|-v         increase verbosity\n"
          "    --version|-V         print version string then exit\n"
          "    --wr_uncor|-w        set an uncorrectable error (no "
          "data transferred)\n"
          "    --xfer_len=BTL|-x BTL    byte transfer length (< 10000) "
          "(default:\n"
          "                             520 bytes)\n\n"
          "Performs a SCSI WRITE LONG (10 or 16) command\n"
          );
}

int main(int argc, char * argv[])
{
    int sg_fd, res, c, infd, offset;
    unsigned char * writeLongBuff = NULL;
    void * rawp = NULL;
    int xfer_len = 520;
    int cor_dis = 0;
    int pblock = 0;
    int wr_uncor = 0;
    int do_16 = 0;
    unsigned long long llba = 0;
    int verbose = 0;
    long long ll;
    int got_stdin;
    char device_name[256];
    char file_name[256];
    char ebuff[EBUFF_SZ];
    const char * ten_or;
    int ret = 1;
    
    memset(device_name, 0, sizeof device_name);
    memset(file_name, 0, sizeof file_name);
    while (1) {
        int option_index = 0;

        c = getopt_long(argc, argv, "chi:l:pSvVwx:", long_options,
                        &option_index);
        if (c == -1)
            break;

        switch (c) {
        case 'c':
            cor_dis = 1;
            break;
        case 'h':
        case '?':
            usage();
            return 0;
        case 'i':
            strncpy(file_name, optarg, sizeof(file_name));
            break;
        case 'l':
            ll = sg_get_llnum(optarg);
            if (-1 == ll) {
                fprintf(stderr, "bad argument to '--lba'\n");
                return SG_LIB_SYNTAX_ERROR;
            }
            llba = (unsigned long long)ll;
            break;
        case 'p':
            pblock = 1;
            break;
        case 'S':
            do_16 = 1;
            break;
        case 'v':
            ++verbose;
            break;
        case 'V':
            fprintf(stderr, ME "version: %s\n", version_str);
            return 0;
        case 'w':
            wr_uncor = 1;
            break;
        case 'x':
            xfer_len = sg_get_num(optarg);
           if (-1 == xfer_len) {
                fprintf(stderr, "bad argument to '--xfer_len'\n");
                return SG_LIB_SYNTAX_ERROR;
            }
            break;
        default:
            fprintf(stderr, "unrecognised option code 0x%x ??\n", c);
            usage();
            return SG_LIB_SYNTAX_ERROR;
        }
    }
    if (optind < argc) {
        if ('\0' == device_name[0]) {
            strncpy(device_name, argv[optind], sizeof(device_name) - 1);
            device_name[sizeof(device_name) - 1] = '\0';
            ++optind;
        }
        if (optind < argc) {
            for (; optind < argc; ++optind)
                fprintf(stderr, "Unexpected extra argument: %s\n",
                        argv[optind]);
            usage();
            return SG_LIB_SYNTAX_ERROR;
        }
    }

    if (0 == device_name[0]) {
        fprintf(stderr, "missing device name!\n");
        usage();
        return SG_LIB_SYNTAX_ERROR;
    }
    if (wr_uncor)
        xfer_len = 0;
    else if (xfer_len >= MAX_XFER_LEN) {
        fprintf(stderr, "xfer_len (%d) is out of range ( < %d)\n",
                xfer_len, MAX_XFER_LEN);
        usage();
        return SG_LIB_SYNTAX_ERROR;
    }
    sg_fd = sg_cmds_open_device(device_name, 0 /* rw */, verbose);
    if (sg_fd < 0) {
        fprintf(stderr, ME "open error: %s: %s\n", device_name,
                safe_strerror(-sg_fd));
        return SG_LIB_FILE_ERROR;
    }
  
    if (wr_uncor) {
        if ('\0' != file_name[0])
            fprintf(stderr, ">>> warning: when '--wr_uncor' given "
                    "'-in=' is ignored\n");
    } else {
        if (NULL == (rawp = malloc(MAX_XFER_LEN))) {
            fprintf(stderr, ME "out of memory\n");
            ret = SG_LIB_FILE_ERROR;
            goto err_out;
        }
        writeLongBuff = (unsigned char *)rawp;
        memset(rawp, 0xff, MAX_XFER_LEN);
        if (file_name[0]) {
            got_stdin = (0 == strcmp(file_name, "-")) ? 1 : 0;
            if (got_stdin)
                infd = 0;
            else {
                if ((infd = open(file_name, O_RDONLY)) < 0) {
                    snprintf(ebuff, EBUFF_SZ,
                             ME "could not open %s for reading", file_name);
                    perror(ebuff);
                    goto err_out;
                }
            }
            res = read(infd, writeLongBuff, xfer_len);
            if (res < 0) {
                snprintf(ebuff, EBUFF_SZ, ME "couldn't read from %s",
                         file_name);
                perror(ebuff);
                if (! got_stdin)
                    close(infd);
                goto err_out;
            }
            if (res < xfer_len) {
                fprintf(stderr, "tried to read %d bytes from %s, got %d "
                        "bytes\n", xfer_len, file_name, res);
                fprintf(stderr, "pad with 0xff bytes and continue\n");
            }
            if (! got_stdin)
                close(infd);
        }
    }
    if (verbose)
        fprintf(stderr, ME "issue write long to device %s\n\t\txfer_len= %d "
                "(0x%x), lba=%" PRIu64 " (0x%" PRIx64 ")\n    cor_dis=%d, "
                "wr_uncor=%d, pblock=%d\n", device_name, xfer_len, xfer_len,
                llba, llba, cor_dis, wr_uncor, pblock);

    ten_or = do_16 ? "16" : "10";
    if (do_16)
        res = sg_ll_write_long16(sg_fd, cor_dis, wr_uncor, pblock, llba,
                                 writeLongBuff, xfer_len, &offset, 1, verbose);
    else
        res = sg_ll_write_long10(sg_fd, cor_dis, wr_uncor, pblock,
                                 (unsigned long)llba, writeLongBuff, xfer_len,
                                 &offset, 1, verbose);
    ret = res;
    switch (res) {
    case 0:
        break;
    case SG_LIB_CAT_NOT_READY:
        fprintf(stderr, "  SCSI WRITE LONG (%s) failed, device not ready\n",
                ten_or);
        break;
    case SG_LIB_CAT_UNIT_ATTENTION:
        fprintf(stderr, "  SCSI WRITE LONG (%s), unit attention\n",
                ten_or);
        break;
    case SG_LIB_CAT_ABORTED_COMMAND:
        fprintf(stderr, "  SCSI WRITE LONG (%s), aborted command\n",
                ten_or);
        break;
    case SG_LIB_CAT_INVALID_OP:
        fprintf(stderr, "  SCSI WRITE LONG (%s) command not supported\n",
                ten_or);
        break;
    case SG_LIB_CAT_ILLEGAL_REQ:
        fprintf(stderr, "  SCSI WRITE LONG (%s) command, bad field in cdb\n",
                ten_or);
        break;
    case SG_LIB_CAT_ILLEGAL_REQ_WITH_INFO:
        fprintf(stderr, "<<< device indicates 'xfer_len' should be %d "
                ">>>\n", xfer_len - offset);
        break;
    default:
        fprintf(stderr, "  SCSI WRITE LONG (%s) command error\n", ten_or);
        break;
    }

err_out:
    if (rawp)
        free(rawp);
    res = sg_cmds_close_device(sg_fd);
    if (res < 0) {
        fprintf(stderr, "close error: %s\n", safe_strerror(-res));
        if (0 == ret)
            return SG_LIB_FILE_ERROR;
    }
    return (ret >= 0) ? ret : SG_LIB_CAT_OTHER;
}