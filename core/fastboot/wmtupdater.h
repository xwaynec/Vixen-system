#ifndef __WMT_UPDATER_H__
#define __WMT_UPDATER_H__
#include "usb.h"
#include "fastboot.h"
#include <stdlib.h>

#ifdef __cplusplus
extern "C"{
#endif


extern usb_handle *open_device(const char *serial);
extern fb_action_list * init_fb_action_list();
extern void uninit_fb_action_list(fb_action_list *);

int close_device(usb_handle *h);

usb_handle * wfb_list_devices(ifc_match_func cb);
//valid ptn : "system" "data"
int wfb_query_ptn_nandinfo(usb_handle *usb, fb_action_list * list, const char *ptn, char *response, size_t responseLen);

int wfb_query_max_download_size(usb_handle *usb, fb_action_list * list, char *response, size_t responseLen);

//valid ptn : "w-load" "u-boot"
int wfb_update_spi(usb_handle *usb, fb_action_list * list, const char *ptn, void *data, unsigned sz);

int wfb_update_env(usb_handle *usb, fb_action_list *list, char *envData, int envSize, char *blacklistData, int blacklistSize);

int wfb_set_env(usb_handle *usb, fb_action_list *list, const char *envname, const char * envvalue);
int wfb_save_env(usb_handle *usb, fb_action_list *list);

//valid ptn : "boot" "recovery" "logo"
int wfb_update_nand(usb_handle *usb, fb_action_list *list, const char*ptn, void *data, unsigned sz);

#ifdef WIN32
int wfb_sparse_start(usb_handle *usb, fb_action_list *list, __int64 filesize);
#else
int wfb_sparse_start(usb_handle *usb, fb_action_list *list, int64_t filesize);
#endif
int wfb_sparse_end(usb_handle *usb, fb_action_list *list);
	
//int wfb_update_nand_sparse(usb_handle *usb, fb_action_list *list, const char *ptn, const char *filename, int maxsize);

int wfb_reboot_device(usb_handle *usb, fb_action_list *list);

#ifdef __cplusplus
}
#endif

#endif
