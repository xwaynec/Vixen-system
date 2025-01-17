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

#include "fastboot.h"
#include "make_ext4fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>


#ifdef USE_MINGW
#include <windows.h>
#include <fcntl.h>
#else
#include <sys/mman.h>
#endif

extern struct fs_info info;

#define ARRAY_SIZE(x)           (sizeof(x)/sizeof(x[0]))

double now()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000;
}

static void outputDebugStr(const char *fmt, ...){
#ifdef USE_MINGW
     char buf[1024]={0};
     va_list ap;
	 va_start(ap, fmt);
	 vsprintf(buf, fmt, ap);
	 va_end(ap);
	 OutputDebugString(buf);
#endif
}


char *mkmsg(const char *fmt, ...)
{
    char buf[256];
    char *s;
    va_list ap;

    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    va_end(ap);

    s = strdup(buf);
    if (s == 0) die("out of memory");
    return s;
}

#define OP_DOWNLOAD   1
#define OP_COMMAND    2
#define OP_QUERY      3
#define OP_NOTICE     4
#define OP_FORMAT     5
#define OP_DOWNLOAD_SPARSE 6

typedef struct Action Action;

#define CMD_SIZE FB_COMMAND_SZ

struct Action
{
    unsigned op;
    Action *next;

    char cmd[CMD_SIZE];
    const char *prod;
    void *data;
    unsigned size;

    char *msg;
    int (*func)(Action *a, int status, char *resp);

    double start;
};

struct fb_action_list{
   Action *action_list;
   Action *action_last;
};

fb_action_list * init_fb_action_list(){
   fb_action_list * list =(fb_action_list *)malloc(sizeof(fb_action_list));
   if(list == NULL) return NULL;
   list->action_list = 0;
   list->action_last = 0;
   return list;
}

void uninit_fb_action_list(fb_action_list * list){
   if(list != NULL) free(list);
}

struct image_data {
    long long partition_size;
    long long image_size; // real size of image file
    void *buffer;
};

void generate_ext4_image(struct image_data *image);
void cleanup_image(struct image_data *image);

int fb_getvar(struct usb_handle *usb, char *response, char *errBuf, int errBufLen, const char *fmt, ...)
{
    char cmd[CMD_SIZE] = "getvar:";
    int getvar_len = strlen(cmd);
    va_list args;

    response[FB_RESPONSE_SZ] = '\0';
    va_start(args, fmt);
    vsnprintf(cmd + getvar_len, sizeof(cmd) - getvar_len, fmt, args);
    va_end(args);
    cmd[CMD_SIZE - 1] = '\0';
    return fb_command_response(usb, cmd, response, errBuf, errBufLen);
}

struct generator {
    char *fs_type;

    /* generate image and return it as image->buffer.
     * size of the buffer returned as image->image_size.
     *
     * image->partition_size specifies what is the size of the
     * file partition we generate image for.
     */
    void (*generate)(struct image_data *image);

    /* it cleans the buffer allocated during image creation.
     * this function probably does free() or munmap().
     */
    void (*cleanup)(struct image_data *image);
} generators[] = {
    { "ext4", generate_ext4_image, cleanup_image }
};

/* Return true if this partition is supported by the fastboot format command.
 * It is also used to determine if we should first erase a partition before
 * flashing it with an ext4 filesystem.  See needs_erase()
 *
 * Not all devices report the filesystem type, so don't report any errors,
 * just return false.
 */
int fb_format_supported(usb_handle *usb, const char *partition, char *errBuf, int errBufLen)
{
    char response[FB_RESPONSE_SZ+1];
    struct generator *generator = NULL;
    int status;
    unsigned int i;

    status = fb_getvar(usb, response, errBuf, errBufLen, "partition-type:%s", partition);
    if (status) {
        return 0;
    }

    for (i = 0; i < ARRAY_SIZE(generators); i++) {
        if (!strncmp(generators[i].fs_type, response, FB_RESPONSE_SZ)) {
            generator = &generators[i];
            break;
        }
    }

    if (generator) {
        return 1;
    }

    return 0;
}

static int cb_default(Action *a, int status, char *resp)
{
    
	if (status) {
        fprintf(stderr,"FAILED (%s)\n", resp);
		outputDebugStr("FAILED (%s)\n", resp);
    } else {
        double split = now();
        fprintf(stderr,"OKAY [%7.3fs]\n", (split - a->start));
		outputDebugStr("OKAY [%7.3fs]\n", (split - a->start));
        a->start = split;
    }
	
    return status;
}

static Action *queue_action(fb_action_list *list, unsigned op, const char *fmt, ...)
{
    Action *a;
    va_list ap;
    size_t cmdsize;

    a = calloc(1, sizeof(Action));
    if (a == 0) die("out of memory");

    va_start(ap, fmt);
    cmdsize = vsnprintf(a->cmd, sizeof(a->cmd), fmt, ap);
    va_end(ap);

    if (cmdsize >= sizeof(a->cmd)) {
        free(a);
        die("Command length (%d) exceeds maximum size (%d)", cmdsize, sizeof(a->cmd));
    }

    if (list->action_last) {
        list->action_last->next = a;
    } else {
        list->action_list = a;
    }
    list->action_last = a;
    a->op = op;
    a->func = cb_default;

    a->start = -1;

    return a;
}

void fb_queue_erase(fb_action_list *list, const char *ptn)
{
    Action *a;
    a = queue_action(list, OP_COMMAND, "erase:%s", ptn);
    a->msg = mkmsg("erasing '%s'", ptn);
}

/* Loads file content into buffer. Returns NULL on error. */
static void *load_buffer(int fd, off_t size)
{
    void *buffer;

#ifdef USE_MINGW
    ssize_t count = 0;

    // mmap is more efficient but mingw does not support it.
    // In this case we read whole image into memory buffer.
    buffer = malloc(size);
    if (!buffer) {
        perror("malloc");
        return NULL;
    }

    lseek(fd, 0, SEEK_SET);
    while(count < size) {
        ssize_t actually_read = read(fd, (char*)buffer+count, size-count);

        if (actually_read == 0) {
            break;
        }
        if (actually_read < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("read");
            free(buffer);
            return NULL;
        }

        count += actually_read;
    }
#else
    buffer = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (buffer == MAP_FAILED) {
        perror("mmap");
        return NULL;
    }
#endif

    return buffer;
}

void cleanup_image(struct image_data *image)
{
#ifdef USE_MINGW
    free(image->buffer);
#else
    munmap(image->buffer, image->image_size);
#endif
}

void generate_ext4_image(struct image_data *image)
{
    int fd;
    struct stat st;

#ifdef USE_MINGW
    /* Ideally we should use tmpfile() here, the same as with unix version.
     * But unfortunately it is not portable as it is not clear whether this
     * function opens file in TEXT or BINARY mode.
     *
     * There are also some reports it is buggy:
     *    http://pdplab.it.uom.gr/teaching/gcc_manuals/gnulib.html#tmpfile
     *    http://www.mega-nerd.com/erikd/Blog/Windiots/tmpfile.html
     */
    char *filename = tempnam(getenv("TEMP"), "fastboot-format.img");
    fd = open(filename, O_RDWR | O_CREAT | O_TRUNC | O_BINARY, 0644);
    unlink(filename);
#else
    fd = fileno(tmpfile());
#endif
    /* reset ext4fs info so we can be called multiple times */
    reset_ext4fs_info();
    info.len = image->partition_size;
    make_ext4fs_internal(fd, NULL, NULL, NULL, 0, 1, 0, 0, 0, NULL);

    fstat(fd, &st);
    image->image_size = st.st_size;
    image->buffer = load_buffer(fd, st.st_size);

    close(fd);
}

int fb_format(Action *a, usb_handle *usb, int skip_if_not_supported, char *errBuf, int errBufLen)
{
    const char *partition = a->cmd;
    char response[FB_RESPONSE_SZ+1];
    int status = 0;
    struct image_data image;
    struct generator *generator = NULL;
    int fd;
    unsigned i;
    char cmd[CMD_SIZE];

    status = fb_getvar(usb, response, errBuf, errBufLen, "partition-type:%s", partition);
    if (status) {
        if (skip_if_not_supported) {
            fprintf(stderr,
                    "Erase successful, but not automatically formatting.\n");
            fprintf(stderr,
                    "Can't determine partition type.\n");
            return 0;
        }
        fprintf(stderr,"FAILED (%s)\n", errBuf);
        return status;
    }

    for (i = 0; i < ARRAY_SIZE(generators); i++) {
        if (!strncmp(generators[i].fs_type, response, FB_RESPONSE_SZ)) {
            generator = &generators[i];
            break;
        }
    }
    if (!generator) {
        if (skip_if_not_supported) {
            fprintf(stderr,
                    "Erase successful, but not automatically formatting.\n");
            fprintf(stderr,
                    "File system type %s not supported.\n", response);
            return 0;
        }
        fprintf(stderr,"Formatting is not supported for filesystem with type '%s'.\n",
                response);
        return -1;
    }

    status = fb_getvar(usb, response, errBuf, errBufLen, "partition-size:%s", partition);
    if (status) {
        if (skip_if_not_supported) {
            fprintf(stderr,
                    "Erase successful, but not automatically formatting.\n");
            fprintf(stderr, "Unable to get partition size\n.");
            return 0;
        }
        fprintf(stderr,"FAILED (%s)\n", errBuf);
        return status;
    }
    image.partition_size = strtoll(response, (char **)NULL, 16);

    generator->generate(&image);
    if (!image.buffer) {
        fprintf(stderr,"Cannot generate image.\n");
        return -1;
    }

    // Following piece of code is similar to fb_queue_flash() but executes
    // actions directly without queuing
    fprintf(stderr, "sending '%s' (%lli KB)...\n", partition, image.image_size/1024);
    status = fb_download_data(usb, image.buffer, image.image_size, errBuf, errBufLen);
    if (status) goto cleanup;

    fprintf(stderr, "writing '%s'...\n", partition);
    snprintf(cmd, sizeof(cmd), "flash:%s", partition);
    status = fb_command(usb, cmd, errBuf, errBufLen);
    if (status) goto cleanup;

cleanup:
    generator->cleanup(&image);

    return status;
}

void fb_queue_format(fb_action_list *list, const char *partition, int skip_if_not_supported)
{
    Action *a;

    a = queue_action(list, OP_FORMAT, partition);
    a->data = (void*)skip_if_not_supported;
    a->msg = mkmsg("formatting '%s' partition", partition);
}

void fb_queue_flash(fb_action_list *list, const char *ptn, void *data, unsigned sz)
{
    Action *a;

    a = queue_action(list, OP_DOWNLOAD, "");
    a->data = data;
    a->size = sz;
    a->msg = mkmsg("sending '%s' (%d KB)", ptn, sz / 1024);

    a = queue_action(list, OP_COMMAND, "flash:%s", ptn);
    a->msg = mkmsg("writing '%s'", ptn);
}

void fb_queue_flash_sparse(fb_action_list *list, const char *ptn, struct sparse_file *s, unsigned sz)
{
    Action *a;

    a = queue_action(list, OP_DOWNLOAD_SPARSE, "");
    a->data = s;
    a->size = 0;
    a->msg = mkmsg("sending sparse '%s' (%d KB)", ptn, sz / 1024);

    a = queue_action(list, OP_COMMAND, "flash:%s", ptn);
    a->msg = mkmsg("writing '%s'", ptn);
}

static int match(char *str, const char **value, unsigned count)
{
    const char *val;
    unsigned n;
    int len;

    for (n = 0; n < count; n++) {
        const char *val = value[n];
        int len = strlen(val);
        int match;

        if ((len > 1) && (val[len-1] == '*')) {
            len--;
            match = !strncmp(val, str, len);
        } else {
            match = !strcmp(val, str);
        }

        if (match) return 1;
    }

    return 0;
}



static int cb_check(Action *a, int status, char *resp, int invert)
{
    const char **value = a->data;
    unsigned count = a->size;
    unsigned n;
    int yes;

    if (status) {
        fprintf(stderr,"FAILED (%s)\n", resp);
        return status;
    }

    if (a->prod) {
        if (strcmp(a->prod, cur_product) != 0) {
            double split = now();
            fprintf(stderr,"IGNORE, product is %s required only for %s [%7.3fs]\n",
                    cur_product, a->prod, (split - a->start));
            a->start = split;
            return 0;
        }
    }

    yes = match(resp, value, count);
    if (invert) yes = !yes;

    if (yes) {
        double split = now();
        fprintf(stderr,"OKAY [%7.3fs]\n", (split - a->start));
        a->start = split;
        return 0;
    }

    fprintf(stderr,"FAILED\n\n");
    fprintf(stderr,"Device %s is '%s'.\n", a->cmd + 7, resp);
    fprintf(stderr,"Update %s '%s'",
            invert ? "rejects" : "requires", value[0]);
    for (n = 1; n < count; n++) {
        fprintf(stderr," or '%s'", value[n]);
    }
    fprintf(stderr,".\n\n");
    return -1;
}

static int cb_require(Action *a, int status, char *resp)
{
    return cb_check(a, status, resp, 0);
}

static int cb_reject(Action *a, int status, char *resp)
{
    return cb_check(a, status, resp, 1);
}

void fb_queue_require(fb_action_list *list, const char *prod, const char *var,
		int invert, unsigned nvalues, const char **value)
{
    Action *a;
    a = queue_action(list, OP_QUERY, "getvar:%s", var);
    a->prod = prod;
    a->data = value;
    a->size = nvalues;
    a->msg = mkmsg("checking %s", var);
    a->func = invert ? cb_reject : cb_require;
    if (a->data == 0) die("out of memory");
}

static int cb_display(Action *a, int status, char *resp)
{
    if (status) {
        fprintf(stderr, "%s FAILED (%s)\n", a->cmd, resp);
		outputDebugStr("%s FAILED (%s)\n", a->cmd, resp);
        return status;
    }
    fprintf(stderr, "%s: %s\n", (char*) a->data, resp);
	outputDebugStr("%s: %s\n", (char*) a->data, resp);
    return 0;
}

void fb_queue_display(fb_action_list *list,const char *var, const char *prettyname)
{
    Action *a;
    a = queue_action(list, OP_QUERY, "getvar:%s", var);
    //let use handle memory alloc/release
	//a->data = strdup(prettyname);
    a->data = (void*)prettyname; 
	if (a->data == 0) die("out of memory");
    a->func = cb_display;
}

static int cb_save(Action *a, int status, char *resp)
{
    if (status) {
        fprintf(stderr, "%s FAILED (%s)\n", a->cmd, resp);
		outputDebugStr("%s FAILED (%s)\n", a->cmd, resp);
        return status;
    }
    strncpy(a->data, resp, a->size);
    return 0;
}

void fb_queue_getvar_save(fb_action_list *list,const char *var, char *dest, unsigned dest_size)
{
    Action *a;
    a = queue_action(list, OP_QUERY, "getvar:%s", var);
    a->data = (void *)dest;
    a->size = dest_size;
    a->func = cb_save;
}

static int cb_do_nothing(Action *a, int status, char *resp)
{
    fprintf(stderr,"\n");
    return 0;
}

void fb_queue_reboot(fb_action_list *list)
{
    Action *a = queue_action(list, OP_COMMAND, "reboot");
    a->func = cb_do_nothing;
    a->msg = strdup("rebooting");
}

static int cb_oem_display(Action *a, int status, char *resp)
{
    if (status) {
        fprintf(stderr, "%s FAILED (%s)\n", a->cmd, resp);
		outputDebugStr("%s FAILED (%s)\n", a->cmd, resp);
        return status;
    }
    fprintf(stderr, "RESULT : %s\n", resp);
	outputDebugStr("RESULT : %s\n", resp);
    return 0;
}

void fb_queue_oemdisplay(fb_action_list *list,const char *cmd, const char *msg){
    Action *a = queue_action(list, OP_QUERY, cmd);
    a->msg = strdup(msg);
	a->func = cb_oem_display;
}

void fb_queue_oemsave(fb_action_list *list,const char*cmd, char *dest, unsigned dest_size){
    Action *a = queue_action(list, OP_QUERY, cmd);
	a->data = (void*)dest;
	a->size = dest_size;
	a->func = cb_save;
}

void fb_queue_command(fb_action_list *list,const char *cmd, const char *msg)
{
    Action *a = queue_action(list, OP_COMMAND, cmd);
    a->msg = strdup(msg);
}

void fb_queue_download(fb_action_list *list,const char *name, void *data, unsigned size)
{
    Action *a = queue_action(list,OP_DOWNLOAD, "");
    a->data = data;
    a->size = size;
    a->msg = mkmsg("downloading '%s'", name);
}

void fb_queue_notice(fb_action_list *list,const char *notice)
{
    Action *a = queue_action(list, OP_NOTICE, "");
    a->data = (void*) notice;
}

void fb_free_list(fb_action_list *list){
    Action *a, *aa;

	if(!(list->action_list)) return;
	
	for (a = list->action_list; a; a = a->next) {
        //Action::msg is Engine.c allocated
        //let user handle Action::data
		free(a->msg);
	}

    a = list->action_list;
	while(a){
        aa = a;
		a = a->next;
		free(aa);
	}

	list->action_list = NULL;
	list->action_last = NULL;
}


int fb_execute_queue(usb_handle *usb, fb_action_list *list)
{
    Action *a;
    char resp[FB_RESPONSE_SZ+1];
    int status = 0;
    char errBuf[1024];

    a = list->action_list;
    if (!a)
        return status;
    resp[FB_RESPONSE_SZ] = 0;


    double start = -1;
    for (a = list->action_list; a; a = a->next) {
        a->start = now();
		if (start < 0) start = a->start;
        if (a->msg) {
            // fprintf(stderr,"%30s... ",a->msg);
            fprintf(stderr,"%s...\n",a->msg);
			outputDebugStr("%s...\n",a->msg);
        }
        memset(errBuf, 0 , sizeof(errBuf));
		
        if (a->op == OP_DOWNLOAD) {
            status = fb_download_data(usb, a->data, a->size, errBuf, sizeof(errBuf));
			status = a->func(a, status, status ? errBuf : "");
            if (status) break;
        } else if (a->op == OP_COMMAND) {
            status = fb_command(usb, a->cmd, errBuf, sizeof(errBuf));
			status = a->func(a, status, status ? errBuf : "");
            if (status) break;
        } else if (a->op == OP_QUERY) {
            status = fb_command_response(usb, a->cmd, resp, errBuf, sizeof(errBuf));
            status = a->func(a, status, status ? errBuf : resp);
            if (status) break;
        } else if (a->op == OP_NOTICE) {
            fprintf(stderr,"%s\n",(char*)a->data);
        } else if (a->op == OP_FORMAT) {
            status = fb_format(a, usb, (int)a->data, errBuf, sizeof(errBuf));
            status = a->func(a, status, status ? errBuf : "");
            if (status) break;
        } else if (a->op == OP_DOWNLOAD_SPARSE) {
            status = fb_download_data_sparse(usb, a->data, errBuf, sizeof(errBuf));
            status = a->func(a, status, status ? errBuf : "");
            if (status) break;
        } else {
            die("bogus action");
        }
    }	

    
    fb_free_list(list);
    fprintf(stderr,"finished. total time: %.3fs\n", (now() - start));
    return status;
}

int fb_queue_is_empty(fb_action_list *list)
{
    return (list->action_list == NULL);
}
