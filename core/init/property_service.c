/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdarg.h>
#include <dirent.h>
#include <limits.h>
#include <errno.h>

#include <cutils/log.h>
#include <cutils/misc.h>
#include <cutils/sockets.h>

#define _REALLY_INCLUDE_SYS__SYSTEM_PROPERTIES_H_
#include <sys/_system_properties.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <sys/atomics.h>
#include <private/android_filesystem_config.h>

#ifdef HAVE_SELINUX
#include <selinux/selinux.h>
#include <selinux/label.h>
#endif

#include "property_service.h"
#include "init.h"
#include "util.h"
#include "log.h"

#define PERSISTENT_PROPERTY_DIR  "/data/property"

static int persistent_properties_loaded = 0;
static int property_area_inited = 0;

static int property_set_fd = -1;
void action_add_queue_tail(struct action *act);

/* White list of permissions for setting property services. */
struct {
    const char *prefix;
    unsigned int uid;
    unsigned int gid;
} property_perms[] = {
    { "net.rmnet0.",      AID_RADIO,    0 },
    { "net.gprs.",        AID_RADIO,    0 },
    { "net.ppp",          AID_RADIO,    0 },
    { "net.qmi",          AID_RADIO,    0 },
    { "net.lte",          AID_RADIO,    0 },
    { "net.cdma",         AID_RADIO,    0 },
    { "ril.",             AID_RADIO,    0 },
    { "gsm.",             AID_RADIO,    0 },
    { "persist.radio",    AID_RADIO,    0 },
    { "net.dns",          AID_RADIO,    0 },
    { "sys.usb.config",   AID_RADIO,    0 },
    { "net.",             AID_SYSTEM,   0 },
    { "dev.",             AID_SYSTEM,   0 },
    { "runtime.",         AID_SYSTEM,   0 },
    { "hw.",              AID_SYSTEM,   0 },
    { "sys.",             AID_SYSTEM,   0 },
    { "service.",         AID_SYSTEM,   0 },
    { "wlan.",            AID_SYSTEM,   0 },
    { "bluetooth.",       AID_BLUETOOTH,   0 },
    { "dhcp.",            AID_SYSTEM,   0 },
    { "dhcp.",            AID_DHCP,     0 },
    { "debug.",           AID_SYSTEM,   0 },
    { "debug.",           AID_SHELL,    0 },
    { "log.",             AID_SHELL,    0 },
    { "service.adb.root", AID_SHELL,    0 },
    { "service.adb.tcp.port", AID_SHELL,    0 },
    { "persist.sys.",     AID_SYSTEM,   0 },
    { "persist.service.", AID_SYSTEM,   0 },
    { "persist.security.", AID_SYSTEM,   0 },
    { "persist.service.bdroid.", AID_BLUETOOTH,   0 },
    { "persist.mtk.wcn.combo.",        AID_SYSTEM,    0 },
    { "selinux."         , AID_SYSTEM,   0 },
    //[Vic] add this  for "sys_prop: permission denied uid:1003  name:service.bootanim.exit"
    { "service.bootanim.exit", AID_GRAPHICS,   0 },
    //[Aksen] add, mediaserver want to set property for whether video is playing, for screen saver
    { "wmt.video.playing", AID_MEDIA,   0 },    
    {"msensor.memsicd",     AID_SYSTEM,   0 },    
    { NULL, 0, 0 }
};

/*
 * White list of UID that are allowed to start/stop services.
 * Currently there are no user apps that require.
 */
struct {
    const char *service;
    unsigned int uid;
    unsigned int gid;
} control_perms[] = {
    { "dumpstate",AID_SHELL, AID_LOG },
    { "ril-daemon",AID_RADIO, AID_RADIO },
     {NULL, 0, 0 }
};

typedef struct {
    void *data;
    size_t size;
    int fd;
} workspace;

static int init_workspace(workspace *w, size_t size)
{
    void *data;
    int fd;

        /* dev is a tmpfs that we can use to carve a shared workspace
         * out of, so let's do that...
         */
    fd = open("/dev/__properties__", O_RDWR | O_CREAT, 0600);
    if (fd < 0)
        return -1;

    if (ftruncate(fd, size) < 0)
        goto out;

    data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(data == MAP_FAILED)
        goto out;

    close(fd);

    fd = open("/dev/__properties__", O_RDONLY);
    if (fd < 0)
        return -1;

    unlink("/dev/__properties__");

    w->data = data;
    w->size = size;
    w->fd = fd;
    return 0;

out:
    close(fd);
    return -1;
}

/* (8 header words + 247 toc words) = 1020 bytes */
/* 1024 bytes header and toc + 247 prop_infos @ 128 bytes = 32640 bytes */

#define PA_COUNT_MAX  247
#define PA_INFO_START 1024
#define PA_SIZE       32768

static workspace pa_workspace;
static prop_info *pa_info_array;

extern prop_area *__system_property_area__;

static int init_property_area(void)
{
    prop_area *pa;

    if(pa_info_array)
        return -1;

    if(init_workspace(&pa_workspace, PA_SIZE))
        return -1;

    fcntl(pa_workspace.fd, F_SETFD, FD_CLOEXEC);

    pa_info_array = (void*) (((char*) pa_workspace.data) + PA_INFO_START);

    pa = pa_workspace.data;
    memset(pa, 0, PA_SIZE);
    pa->magic = PROP_AREA_MAGIC;
    pa->version = PROP_AREA_VERSION;

        /* plug into the lib property services */
    __system_property_area__ = pa;
    property_area_inited = 1;
    return 0;
}

static void update_prop_info(prop_info *pi, const char *value, unsigned len)
{
    pi->serial = pi->serial | 1;
    memcpy(pi->value, value, len + 1);
    pi->serial = (len << 24) | ((pi->serial + 1) & 0xffffff);
    __futex_wake(&pi->serial, INT32_MAX);
}

static int check_mac_perms(const char *name, char *sctx)
{
#ifdef HAVE_SELINUX
    if (is_selinux_enabled() <= 0)
        return 1;

    char *tctx = NULL;
    const char *class = "property_service";
    const char *perm = "set";
    int result = 0;

    if (!sctx)
        goto err;

    if (!sehandle_prop)
        goto err;

    if (selabel_lookup(sehandle_prop, &tctx, name, 1) != 0)
        goto err;

    if (selinux_check_access(sctx, tctx, class, perm, name) == 0)
        result = 1;

    freecon(tctx);
 err:
    return result;

#endif
    return 1;
}

static int check_control_mac_perms(const char *name, char *sctx)
{
#ifdef HAVE_SELINUX

    /*
     *  Create a name prefix out of ctl.<service name>
     *  The new prefix allows the use of the existing
     *  property service backend labeling while avoiding
     *  mislabels based on true property prefixes.
     */
    char ctl_name[PROP_VALUE_MAX+4];
    int ret = snprintf(ctl_name, sizeof(ctl_name), "ctl.%s", name);

    if (ret < 0 || (size_t) ret >= sizeof(ctl_name))
        return 0;

    return check_mac_perms(ctl_name, sctx);

#endif
    return 1;
}

/*
 * Checks permissions for starting/stoping system services.
 * AID_SYSTEM and AID_ROOT are always allowed.
 *
 * Returns 1 if uid allowed, 0 otherwise.
 */
static int check_control_perms(const char *name, unsigned int uid, unsigned int gid, char *sctx) {

    int i;
    if (uid == AID_SYSTEM || uid == AID_ROOT)
      return check_control_mac_perms(name, sctx);

    /* Search the ACL */
    for (i = 0; control_perms[i].service; i++) {
        if (strcmp(control_perms[i].service, name) == 0) {
            if ((uid && control_perms[i].uid == uid) ||
                (gid && control_perms[i].gid == gid)) {
                return check_control_mac_perms(name, sctx);
            }
        }
    }
    return 0;
}

/*
 * Checks permissions for setting system properties.
 * Returns 1 if uid allowed, 0 otherwise.
 */
static int check_perms(const char *name, unsigned int uid, unsigned int gid, char *sctx)
{
    int i;
    if(!strncmp(name, "ro.", 3))
        name +=3;

    if (uid == 0)
        return check_mac_perms(name, sctx);

    //[Bobor] add for allow any process to set wmt prop
    #define WMT_PROP_NAME_HEAD "wmt."
    #define WMT_PROP_NAME_BODY ".wmt."
    if (strncmp(name, WMT_PROP_NAME_HEAD, strlen(WMT_PROP_NAME_HEAD)) == 0
        || strstr(name, WMT_PROP_NAME_BODY) != NULL) {
        return 1;
    }
    
    for (i = 0; property_perms[i].prefix; i++) {
        if (strncmp(property_perms[i].prefix, name,
                    strlen(property_perms[i].prefix)) == 0) {
            if ((uid && property_perms[i].uid == uid) ||
                (gid && property_perms[i].gid == gid)) {

                return check_mac_perms(name, sctx);
            }
        }
    }

    return 0;
}

const char* property_get(const char *name)
{
    prop_info *pi;

    if(strlen(name) >= PROP_NAME_MAX) return 0;

    pi = (prop_info*) __system_property_find(name);

    if(pi != 0) {
        return pi->value;
    } else {
        return 0;
    }
}

static void write_persistent_property(const char *name, const char *value)
{
    const char *tempPath = PERSISTENT_PROPERTY_DIR "/.temp";
    char path[PATH_MAX];
    int fd, length;

    snprintf(path, sizeof(path), "%s/%s", PERSISTENT_PROPERTY_DIR, name);

    fd = open(tempPath, O_WRONLY|O_CREAT|O_TRUNC, 0600);
    if (fd < 0) {
        ERROR("Unable to write persistent property to temp file %s errno: %d\n", tempPath, errno);
        return;
    }
    write(fd, value, strlen(value));
    close(fd);

    if (rename(tempPath, path)) {
        unlink(tempPath);
        ERROR("Unable to rename persistent property file %s to %s\n", tempPath, path);
    }
}

int property_set(const char *name, const char *value)
{
    prop_area *pa;
    prop_info *pi;

    int namelen = strlen(name);
    int valuelen = strlen(value);

    if(namelen >= PROP_NAME_MAX) return -1;
    if(valuelen >= PROP_VALUE_MAX) return -1;
    if(namelen < 1) return -1;

    pi = (prop_info*) __system_property_find(name);

    if(pi != 0) {
        /* ro.* properties may NEVER be modified once set */
        if(!strncmp(name, "ro.", 3)){
            if(!strcmp(name, "ro.secure") || !strcmp(name, "ro.debuggable"))
                INFO("Enable %s be overwrite!\n", name);
            else
                return -1;
        }

        pa = __system_property_area__;
        update_prop_info(pi, value, valuelen);
        pa->serial++;
        __futex_wake(&pa->serial, INT32_MAX);
    } else {
        pa = __system_property_area__;
        if(pa->count == PA_COUNT_MAX) return -1;

        pi = pa_info_array + pa->count;
        pi->serial = (valuelen << 24);
        memcpy(pi->name, name, namelen + 1);
        memcpy(pi->value, value, valuelen + 1);

        pa->toc[pa->count] =
            (namelen << 24) | (((unsigned) pi) - ((unsigned) pa));

        pa->count++;
        pa->serial++;
        __futex_wake(&pa->serial, INT32_MAX);
    }
    /* If name starts with "net." treat as a DNS property. */
    if (strncmp("net.", name, strlen("net.")) == 0)  {
        if (strcmp("net.change", name) == 0) {
            return 0;
        }
       /*
        * The 'net.change' property is a special property used track when any
        * 'net.*' property name is updated. It is _ONLY_ updated here. Its value
        * contains the last updated 'net.*' property.
        */
        property_set("net.change", name);
    } else if (persistent_properties_loaded &&
            strncmp("persist.", name, strlen("persist.")) == 0) {
        /*
         * Don't write properties to disk until after we have read all default properties
         * to prevent them from being overwritten by default values.
         */
        write_persistent_property(name, value);
#ifdef HAVE_SELINUX
    } else if (strcmp("selinux.reload_policy", name) == 0 &&
               strcmp("1", value) == 0) {
        selinux_reload_policy();
#endif
    }
    property_changed(name, value);
    return 0;
}

void handle_property_set_fd()
{
    prop_msg msg;
    int s;
    int r;
    int res;
    struct ucred cr;
    struct sockaddr_un addr;
    socklen_t addr_size = sizeof(addr);
    socklen_t cr_size = sizeof(cr);
    char * source_ctx = NULL;

    if ((s = accept(property_set_fd, (struct sockaddr *) &addr, &addr_size)) < 0) {
        return;
    }

    /* Check socket options here */
    if (getsockopt(s, SOL_SOCKET, SO_PEERCRED, &cr, &cr_size) < 0) {
        close(s);
        ERROR("Unable to recieve socket options\n");
        return;
    }

    r = TEMP_FAILURE_RETRY(recv(s, &msg, sizeof(msg), 0));
    if(r != sizeof(prop_msg)) {
        ERROR("sys_prop: mis-match msg size recieved: %d expected: %d errno: %d\n",
              r, sizeof(prop_msg), errno);
        close(s);
        return;
    }

    switch(msg.cmd) {
    case PROP_MSG_SETPROP:
        msg.name[PROP_NAME_MAX-1] = 0;
        msg.value[PROP_VALUE_MAX-1] = 0;

#ifdef HAVE_SELINUX
        getpeercon(s, &source_ctx);
#endif

        if(memcmp(msg.name,"ctl.",4) == 0) {
            // Keep the old close-socket-early behavior when handling
            // ctl.* properties.
            close(s);
            if (check_control_perms(msg.value, cr.uid, cr.gid, source_ctx)) {
                handle_control_message((char*) msg.name + 4, (char*) msg.value);
            } else {
                ERROR("sys_prop: Unable to %s service ctl [%s] uid:%d gid:%d pid:%d\n",
                        msg.name + 4, msg.value, cr.uid, cr.gid, cr.pid);
            }
        } else {
            if (check_perms(msg.name, cr.uid, cr.gid, source_ctx)) {
                property_set((char*) msg.name, (char*) msg.value);
            } else {
                ERROR("sys_prop: permission denied uid:%d  name:%s\n",
                      cr.uid, msg.name);
            }

            // Note: bionic's property client code assumes that the
            // property server will not close the socket until *AFTER*
            // the property is written to memory.
            close(s);
        }

		
#ifdef HAVE_SELINUX
        freecon(source_ctx);
#endif

        break;

    default:
        close(s);
        break;
    }
}

void get_property_workspace(int *fd, int *sz)
{
    *fd = pa_workspace.fd;
    *sz = pa_workspace.size;
}

static void load_properties(char *data)
{
    char *key, *value, *eol, *sol, *tmp;

    sol = data;
    while((eol = strchr(sol, '\n'))) {
        key = sol;
        *eol++ = 0;
        sol = eol;

        value = strchr(key, '=');
        if(value == 0) continue;
        *value++ = 0;

        while(isspace(*key)) key++;
        if(*key == '#') continue;
        tmp = value - 2;
        while((tmp > key) && isspace(*tmp)) *tmp-- = 0;

        while(isspace(*value)) value++;
        tmp = eol - 2;
        while((tmp > value) && isspace(*tmp)) *tmp-- = 0;

        property_set(key, value);
    }
}

static void load_properties_from_file(const char *fn)
{
    char *data;
    unsigned sz;

    data = read_file(fn, &sz);

    if(data != 0) {
        load_properties(data);
        free(data);
    }
}

static void load_persistent_properties()
{
    DIR* dir = opendir(PERSISTENT_PROPERTY_DIR);
    struct dirent*  entry;
    char path[PATH_MAX];
    char value[PROP_VALUE_MAX];
    int fd, length;

    if (dir) {
        while ((entry = readdir(dir)) != NULL) {
            if (strncmp("persist.", entry->d_name, strlen("persist.")))
                continue;
#if HAVE_DIRENT_D_TYPE
            if (entry->d_type != DT_REG)
                continue;
#endif
            /* open the file and read the property value */
            snprintf(path, sizeof(path), "%s/%s", PERSISTENT_PROPERTY_DIR, entry->d_name);
            fd = open(path, O_RDONLY);
            if (fd >= 0) {
                length = read(fd, value, sizeof(value) - 1);
                if (length >= 0) {
                    value[length] = 0;
                    property_set(entry->d_name, value);
                } else {
                    ERROR("Unable to read persistent property file %s errno: %d\n", path, errno);
                }
                close(fd);
            } else {
                ERROR("Unable to open persistent property file %s errno: %d\n", path, errno);
            }
        }
        closedir(dir);
    } else {
        ERROR("Unable to open persistent property directory %s errno: %d\n", PERSISTENT_PROPERTY_DIR, errno);
    }

    persistent_properties_loaded = 1;
}

void property_init(void)
{
    init_property_area();
}

void property_load_boot_defaults(void)
{
    load_properties_from_file(PROP_PATH_RAMDISK_DEFAULT);
}

int properties_inited(void)
{
    return property_area_inited;
}

static void load_override_properties() {
#ifdef ALLOW_LOCAL_PROP_OVERRIDE
    const char *debuggable = property_get("ro.debuggable");
    if (debuggable && (strcmp(debuggable, "1") == 0)) {
        load_properties_from_file(PROP_PATH_LOCAL_OVERRIDE);
    }
#endif /* ALLOW_LOCAL_PROP_OVERRIDE */
}


/* When booting an encrypted system, /data is not mounted when the
 * property service is started, so any properties stored there are
 * not loaded.  Vold triggers init to load these properties once it
 * has mounted /data.
 */
void load_persist_props(void)
{
    load_override_properties();
    /* Read persistent properties after all default values have been loaded. */
    load_persistent_properties();
}

#define MAIN_DEV_PROP_NAME "persist.wmt.maindev"
#define MAIN_DEV_PROP_SDCARD "1"
#define MAIN_DEV_PROP_ROM    "0"
#define WMT_MAIN_DEV "wmt.main.externaldev"
#define WMT_DEV_SD   "sdcard"

void start_property_service(void)
{
    int fd;

    load_properties_from_file(PROP_PATH_SYSTEM_BUILD);
    load_properties_from_file(PROP_PATH_SYSTEM_DEFAULT);
    load_override_properties();
    /* Read persistent properties after all default values have been loaded. */
    load_persistent_properties();

	ERROR("%s loaded here", PROP_PATH_SYSTEM_DEFAULT);	
	const char * external = property_get(MAIN_DEV_PROP_NAME);
	char maindev[512]={0};
	int size = sizeof(maindev);

	const char* enabled = property_get("persist.wmt.app2sd.enable");
	if (enabled != NULL && !strcmp(enabled, "true")) {
		wmt_getsyspara(WMT_MAIN_DEV, maindev, &size);
		if(!strcmp(maindev, WMT_DEV_SD)){
			if(external == NULL || strcmp(MAIN_DEV_PROP_SDCARD, external)){
				property_set(MAIN_DEV_PROP_NAME, MAIN_DEV_PROP_SDCARD);
				ERROR("wmt.main.externaldev is %s, persist.wmt.maindev is %s", maindev, external==NULL?"null":external);
			}
		} else {
			if(strlen(maindev) <= 0 ||external == NULL || strcmp(MAIN_DEV_PROP_ROM, external)){
				property_set(MAIN_DEV_PROP_NAME, MAIN_DEV_PROP_ROM);
				ERROR("wmt.main.externaldev is %s, persist.wmt.maindev is %s", maindev, external);			
			}
		}
	}

	char modemType[8] = {'\0', };
	char modemEnable[8] = {'\0', };
	int len = 8;
	const char* enable = property_get("ril.modem.enable");
	if(enable)
	{
		if(atoi(enable) == 1)
		{
			FILE* fp = fopen("/etc/ppp/ril.modem.type", "rb");
			if(fp)
			{
				fread(modemType, 7, 1, fp);
				if(strlen(modemType) > 0)
				{
					modemType[strlen(modemType) - 1] = '\0';
					property_set("ril.modem.type", modemType);
				}
				fclose(fp);
			}
			else
				ERROR("modem type is not set???");
			fp = fopen("/etc/ppp/ril.modem.type2", "rb");
			if(fp)
			{
				memset(modemType, 0, 8);
				fread(modemType, 7, 1, fp);
				if(strlen(modemType) > 0)
				{
					modemType[strlen(modemType) - 1] = '\0';
					property_set("ril.modem.type2", modemType);
				}
				fclose(fp);
			}
		}
		else
			ERROR("ril is not config...");
	}
	else
		ERROR("Maybe ril is not enable...");

    fd = create_socket(PROP_SERVICE_NAME, SOCK_STREAM, 0666, 0, 0);
    if(fd < 0) return;
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    fcntl(fd, F_SETFL, O_NONBLOCK);

    listen(fd, 8);
    property_set_fd = fd;
}

int get_property_set_fd()
{
    return property_set_fd;
}
