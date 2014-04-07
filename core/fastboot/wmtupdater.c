#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdint.h>
#include "fastboot.h"
#include "wmtupdater.h"

int close_device(usb_handle *h){
    return usb_close(h);
}



usb_handle *  wfb_list_devices(ifc_match_func cb){
    return usb_open(cb, NULL);
}
//valid ptn : "system" "data"
int wfb_query_ptn_nandinfo(usb_handle *usb, fb_action_list * list, const char *ptn, char *response, size_t responseLen){
    if(!strcmp(ptn, "system") || !strcmp(ptn, "data")){
        char cmd[64]={0};
        snprintf(cmd, sizeof(cmd), "oem nand:%s", ptn);
        fb_queue_oemsave(list, cmd, response, responseLen);
        return fb_execute_queue(usb, list);
    }
    return -1;
}

int wfb_query_max_download_size(usb_handle *usb, fb_action_list * list, char*response, size_t responseLen){
    char cmd[64]={0};
    strcpy(cmd,"oem max-download-size");
    fb_queue_oemsave(list, cmd, response, responseLen);
    return fb_execute_queue(usb, list);
}

//valid ptn : "w-load" "u-boot" "env1" "env2"
int wfb_update_spi(usb_handle *usb,fb_action_list * list, const char *ptn, void *data, unsigned sz){
    if(!strcmp(ptn, "w-load") || !strcmp(ptn, "u-boot") || !strcmp(ptn, "env1") || !strcmp(ptn, "env2")){
        fb_queue_flash(list, ptn, data, sz);
        return fb_execute_queue(usb, list);
    }
    return -1;
}


int wfb_update_env(usb_handle *usb, fb_action_list *list, char *envData, int envSize, char *blacklistData, int blacklistSize){
          
    //download envfile to memory address 192M and blacklistfile to memory address 224M    
    fb_queue_flash(list, "ram:C000000", envData, envSize);
    fb_queue_flash(list, "ram:E000000", blacklistData, blacklistSize);
    fb_queue_command(list, "oem syncenv:C000000 E000000", "update env...");
    return fb_execute_queue(usb, list);
    
}


int wfb_set_env(usb_handle *usb, fb_action_list *list, const char *envname, const char * envvalue){
    char buf[2048]={0};
    snprintf(buf, sizeof(buf), "oem setenv:%s=%s", envname, envvalue);
    fb_queue_command(list, buf, "set env");
    return fb_execute_queue(usb, list);
}
int wfb_save_env(usb_handle *usb, fb_action_list *list){
    fb_queue_command(list, "oem saveenv", "save env");
    return fb_execute_queue(usb, list);
}

//valid ptn : "boot" "recovery" "logo"
int wfb_update_nand(usb_handle *usb,fb_action_list * list, const char*ptn, void *data, unsigned sz){
    if(!strcmp(ptn, "boot") || !strcmp(ptn, "recovery") || !strcmp(ptn, "logo") ||
       !strcmp(ptn, "system") || !strcmp(ptn, "data")){
        fb_queue_flash(list, ptn, data, sz);
        return fb_execute_queue(usb,list);
    }
    return -1;
}


int wfb_sparse_start(usb_handle *usb, fb_action_list *list, int64_t filesize){
    char cmd[64]={0};
    snprintf(cmd, sizeof(cmd), "oem sparsedownload:%lld", filesize);
    fb_queue_command(list, cmd, "sparse download start...");
    return fb_execute_queue(usb, list);    
}

int wfb_sparse_end(usb_handle *usb, fb_action_list *list){
    char cmd[64]={0};
    strcpy(cmd, "oem sparsedownload end");
    fb_queue_command(list, cmd, "sparse download end...");
    return fb_execute_queue(usb, list);
}

int wfb_reboot_device(usb_handle *usb, fb_action_list * list){
    fb_queue_reboot(list);
    return fb_execute_queue(usb, list);
}
