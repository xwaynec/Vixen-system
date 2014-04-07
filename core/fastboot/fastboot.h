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

#ifndef _FASTBOOT_H_
#define _FASTBOOT_H_
#include "usb.h"

#ifdef __cplusplus
extern "C"{
#endif

struct sparse_file;
typedef struct fb_action_list fb_action_list;

/* protocol.c - fastboot protocol */
int fb_command(usb_handle *usb, const char *cmd, char *errBuf, int errBufLen);
int fb_command_response(usb_handle *usb, const char *cmd, char *response, char *errBuf, int errBufLen);
int fb_download_data(usb_handle *usb, const void *data, unsigned size, char *errBuf, int errBufLen);
int fb_download_data_sparse(usb_handle *usb, struct sparse_file *s, char *errBuf, int errBufLen);
//char *fb_get_error(void);

#define FB_COMMAND_SZ 64
#define FB_RESPONSE_SZ 64

/* engine.c - high level command queue engine */
int fb_getvar(struct usb_handle *usb, char *response, char *errBuf, int errBufLen, const char *fmt, ...);
int fb_format_supported(usb_handle *usb, const char *partition, char *errBuf, int errBufLen);
void fb_queue_oemsave(fb_action_list *list,const char*cmd, char *dest, unsigned dest_size);
void fb_queue_flash(fb_action_list *list, const char *ptn, void *data, unsigned sz);
void fb_queue_flash_sparse(fb_action_list *list,const char *ptn, struct sparse_file *s, unsigned sz);
void fb_queue_erase(fb_action_list *list,const char *ptn);
void fb_queue_format(fb_action_list *list,const char *ptn, int skip_if_not_supported);
void fb_queue_require(fb_action_list *list,const char *prod, const char *var, int invert,
        unsigned nvalues, const char **value);
void fb_queue_display(fb_action_list *list,const char *var, const char *prettyname);
void fb_queue_getvar_save(fb_action_list *list,const char *var, char *dest, unsigned dest_size);
void fb_queue_reboot(fb_action_list *list);
void fb_queue_command(fb_action_list *list,const char *cmd, const char *msg);
void fb_queue_download(fb_action_list *list,const char *name, void *data, unsigned size);
void fb_queue_notice(fb_action_list *list,const char *notice);
int fb_execute_queue(usb_handle *usb,fb_action_list *list);
int fb_queue_is_empty(fb_action_list *list);

/* util stuff */
void die(const char *fmt, ...);

/* Current product */
extern char cur_product[FB_RESPONSE_SZ + 1];

int fastboot_main(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif
