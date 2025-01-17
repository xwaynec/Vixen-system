/*
 * Copyright (C) 2008 The Android Open Source Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#define min(a, b) \
    ({ typeof(a) _a = (a); typeof(b) _b = (b); (_a < _b) ? _a : _b; })
#define round_down(a, b) \
    ({ typeof(a) _a = (a); typeof(b) _b = (b); _a - (_a % _b); })

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <sparse/sparse.h>

#include "fastboot.h"

//static char ERROR[128];

//char *fb_get_error(void)
//{
//    return ERROR;
//}

static int check_response(usb_handle *usb, unsigned int size, char *response, char *errBuf, int errBufLen)
{
    unsigned char status[FB_RESPONSE_SZ+1];
    int r;

    for(;;) {
        r = usb_read(usb, status, FB_RESPONSE_SZ);
        if(r < 0) {
            snprintf(errBuf, errBufLen, "status read failed (%s)", strerror(errno));
            //usb_close(usb);
            return -1;
        }
        status[r] = 0;

        if(r < 4) {
            snprintf(errBuf, errBufLen, "status malformed (%d bytes)", r);
            //usb_close(usb);
            return -1;
        }

        if(!memcmp(status, "INFO", 4)) {
            fprintf(stderr,"(bootloader) %s\n", status + 4);
            continue;
        }

        if(!memcmp(status, "OKAY", 4)) {
            if(response) {
                strcpy(response, (char*) status + 4);
            }
            return 0;
        }

        if(!memcmp(status, "FAIL", 4)) {
            if(r > 4) {
                snprintf(errBuf, errBufLen, "remote: %s", status + 4);
            } else {
                snprintf(errBuf, errBufLen, "remote failure");
            }
            return -1;
        }

        if(!memcmp(status, "DATA", 4) && size > 0){
            unsigned dsize = strtoul((char*) status + 4, 0, 16);
            if(dsize > size) {
                snprintf(errBuf, errBufLen, "data size too large(%d > %d)", dsize, size);
                //usb_close(usb);
                return -1;
            }
            return dsize;
        }

        snprintf(errBuf, errBufLen,"unknown status code");
        //usb_close(usb);
        break;
    }

    return -1;
}

static int _command_start(usb_handle *usb, const char *cmd, unsigned size,
                          char *response, char *errBuf, int errBufLen)
{
    int cmdsize = strlen(cmd);
    int r;

    if(response) {
        response[0] = 0;
    }

    if(cmdsize > FB_COMMAND_SZ) {
        snprintf(errBuf, errBufLen,"command too large");
        return -1;
    }

    if(usb_write(usb, cmd, cmdsize) != cmdsize) {
        snprintf(errBuf, errBufLen,"command write failed (%s)", strerror(errno));
        //usb_close(usb);
        return -1;
    }

    return check_response(usb, size, response, errBuf, errBufLen);
}

static int _command_data(usb_handle *usb, const void *data, unsigned size, char *errBuf, int errBufLen)
{
    int r;

    r = usb_write(usb, data, size);
    if(r < 0) {
        snprintf(errBuf, errBufLen, "data transfer failure (%d, %s)",errno, strerror(errno));
        //usb_close(usb);
        return -1;
    }
    if(r != ((int) size)) {
        snprintf(errBuf, errBufLen, "data transfer failure (short transfer)");
        //usb_close(usb);
        return -1;
    }

    return r;
}

static int _command_end(usb_handle *usb, char *errBuf, int errBufLen)
{
    int r;
    r = check_response(usb, 0, 0, errBuf, errBufLen);
    if(r < 0) {
        return -1;
    }
    return 0;
}

static int _command_send(usb_handle *usb, const char *cmd,
                         const void *data, unsigned size,
                         char *response, char *errBuf, int errBufLen)
{
    int r;
    if (size == 0) {
        return -1;
    }

    r = _command_start(usb, cmd, size, response, errBuf, errBufLen);
    if (r < 0) {
        return -1;
    }

    r = _command_data(usb, data, size, errBuf, errBufLen);
    if (r < 0) {
        return -1;
    }

    r = _command_end(usb, errBuf, errBufLen);
    if(r < 0) {
        return -1;
    }

    return size;
}

static int _command_send_no_data(usb_handle *usb, const char *cmd,
                                 char *response, char *errBuf, int errBufLen)
{
    int r;

    return _command_start(usb, cmd, 0, response, errBuf, errBufLen);
}

int fb_command(usb_handle *usb, const char *cmd, char *errBuf, int errBufLen)
{
    return _command_send_no_data(usb, cmd, 0, errBuf, errBufLen);
}

int fb_command_response(usb_handle *usb, const char *cmd, char *response, char *errBuf, int errBufLen)
{
    return _command_send_no_data(usb, cmd, response, errBuf, errBufLen);
}

int fb_download_data(usb_handle *usb, const void *data, unsigned size, char *errBuf, int errBufLen)
{
    char cmd[FB_COMMAND_SZ];
    int r;

    sprintf(cmd, "download:%08x", size);
    r = _command_send(usb, cmd, data, size, 0, errBuf, errBufLen);

    if(r < 0) {
        return -1;
    } else {
        return 0;
    }
}

#define USB_BUF_SIZE 512


typedef struct {
    usb_handle *usb;
	char usb_buf[USB_BUF_SIZE];
	int usb_buf_len;
	char *errBuf;
	int errBufLen;
}sparse_context;

static int fb_download_data_sparse_write(void *priv, const void *data, int len)
{
    int r;
    sparse_context *context = priv;
    int to_write;
    const char *ptr = data;

    if (context->usb_buf_len) {
        to_write = min(USB_BUF_SIZE - context->usb_buf_len, len);

        memcpy(context->usb_buf + context->usb_buf_len, ptr, to_write);
        context->usb_buf_len += to_write;
        ptr += to_write;
        len -= to_write;
    }

    if (context->usb_buf_len == USB_BUF_SIZE) {
        r = _command_data(context->usb, context->usb_buf, USB_BUF_SIZE, context->errBuf, context->errBufLen);
        if (r != USB_BUF_SIZE) {
            return -1;
        }
        context->usb_buf_len = 0;
    }

    if (len > USB_BUF_SIZE) {
        if (context->usb_buf_len > 0) {
            snprintf(context->errBuf, context->errBufLen, "internal error: usb_buf not empty\n");
            return -1;
        }
        to_write = round_down(len, USB_BUF_SIZE);
        r = _command_data(context->usb, ptr, to_write, context->errBuf, context->errBufLen);
        if (r != to_write) {
            return -1;
        }
        ptr += to_write;
        len -= to_write;
    }

    if (len > 0) {
        if (len > USB_BUF_SIZE) {
            snprintf(context->errBuf, context->errBufLen, "internal error: too much left for usb_buf\n");
            return -1;
        }
        memcpy(context->usb_buf, ptr, len);
        context->usb_buf_len = len;
    }

    return 0;
}

static int fb_download_data_sparse_flush(sparse_context *context, char *errBuf, int errBufLen)
{
    int r;

    if (context->usb_buf_len > 0) {
        r = _command_data(context->usb, context->usb_buf, context->usb_buf_len, errBuf, errBufLen);
        if (r != context->usb_buf_len) {
            return -1;
        }
        context->usb_buf_len = 0;
    }

    return 0;
}

int fb_download_data_sparse(usb_handle *usb, struct sparse_file *s, char *errBuf, int errBufLen)
{
    char cmd[FB_COMMAND_SZ];
    int r;
    int size = sparse_file_len(s, true, false);
    sparse_context * context = NULL;

	if (size <= 0) {
        return -1;
    }

	context = malloc(sizeof(sparse_context));
	if(context == NULL){
        return -1;
	}

    memset(context, 0, sizeof(sparse_context));
    context->usb = usb;
	context->errBuf = errBuf;
	context->errBufLen = errBufLen;
    sprintf(cmd, "download:%08x", size);
    r = _command_start(usb, cmd, size, 0, errBuf, errBufLen);
    if (r < 0) {
        return -1;
    }

	
    r = sparse_file_callback(s, true, false, fb_download_data_sparse_write, context);
    if (r < 0) {
        return -1;
    }

    fb_download_data_sparse_flush(context, errBuf, errBufLen);

    free(context);

    return _command_end(usb, errBuf, errBufLen);
}
