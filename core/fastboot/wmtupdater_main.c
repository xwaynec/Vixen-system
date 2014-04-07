#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <convyaffs2image.h>
#include "wmtupdater.h"
#include <windows.h>

//NOTICE: use below function in vc may cause problem
//        malloc in mingw and free in vc cause problem as vc insert debuginfo in heap allocation
extern void *load_file(const char *fn, unsigned *_sz);
extern int64_t file_size(const char *fn);

static void usage(void)
{
    fprintf(stderr,
/*           1234567890123456789012345678901234567890123456789012345678901234567890123456 */
            "usage: wmtupdater [ <option> ]\n"
            "\n"
            "options:\n"
            "  -w <wload image>                         update wload image\n"
            "  -u <uboot image>                         update uboot image\n"
            "  -b <boot image>                          update boot image\n"
            "  -r <recovery image>                      update recovery image\n"
            "  -s <system image>                        update system image\n"
            "  -d <data image>                          update data image\n"
            "  -l <logo>                                update logo\n"                        
            "  -S <specific device>                     Specify device serial number\n"
            "  -R                                       Reboot\n"
            "  -D                                       Delete temp files\n"
            "  -B                                       env black list\n"
            "  -e                                       env image file\n"
        );
}

/*
static int match_fastboot_with_serial(usb_ifc_info *info, const char *local_serial)
{
    if(!(vendor_id && (info->dev_vendor == vendor_id)) &&
       (info->dev_vendor != 0x18d1) &&  // Google
       (info->dev_vendor != 0x8087) &&  // Intel
       (info->dev_vendor != 0x0451) &&
       (info->dev_vendor != 0x0502) &&
       (info->dev_vendor != 0x0fce) &&  // Sony Ericsson
       (info->dev_vendor != 0x05c6) &&  // Qualcomm
       (info->dev_vendor != 0x22b8) &&  // Motorola
       (info->dev_vendor != 0x0955) &&  // Nvidia
       (info->dev_vendor != 0x413c) &&  // DELL
       (info->dev_vendor != 0x2314) &&  // INQ Mobile
       (info->dev_vendor != 0x0b05) &&  // Asus
       (info->dev_vendor != 0x040d) &&  // WMT       
       (info->dev_vendor != 0x0bb4))    // HTC
            return -1;
    if(info->ifc_class != 0xff) return -1;
    if(info->ifc_subclass != 0x42) return -1;
    if(info->ifc_protocol != 0x03) return -1;
    // require matching serial number or device path if requested
    // at the command line with the -s option.
    if (local_serial && (strcmp(local_serial, info->serial_number) != 0 &&
                   strcmp(local_serial, info->device_path) != 0)) return -1;
    return 0;
}
*/

static int list_devices_callback(usb_ifc_info *info, const char *s)
{
    //if (match_fastboot_with_serial(info, NULL) == 0) {
        char* serial = info->serial_number;
        if (!info->writable) {
            serial = "no permissions"; // like "adb devices"
        }
        if (!serial[0]) {
            serial = "????????????";
        }
        // output compatible with "adb devices"
        if (!info->device_path) {
            printf("%-22s wmtupdater\n", serial);
        } else {
            printf("%-22s wmtupdater %s\n", serial, info->device_path);
        }
    //}

    //NOTICE: wfb_list_devices calls usb_open to emuerate device, and usb_open on windows(usb_windows.c) has some special requirements:
    //        if we return zero here, usb_open will found a matched device and return usb_handle*, and you should call usb_close,
    //        otherwise, next usb_open call on the same device call will return null.
    //        return non-zero here, usb_open will never found a matched device and we can ignore usb_close call. 
    //        
    return -1;
}

static int update_spi(usb_handle *usb, fb_action_list *list, char *ptn, char * file){
    void * content = NULL;
    unsigned sz;
    int status = 0;
    if(file != NULL){
        content = load_file(file, &sz);
        if(content == NULL){
            printf("file to open %s %s\n",ptn, file);
            perror("reason");
            return -1;
        }
        status = wfb_update_spi(usb, list, ptn, content, sz);
        free(content);
        if(status < 0){
           printf("shit happens while updating %s\n", ptn);   
           return -1;
        }
    }
    return 0;
}

static int update_env(usb_handle *usb, fb_action_list *actionlist, const char *envfile, const char *blacklistfile){

    FILE * ef = NULL, * bf = NULL;
    char * ef_buf = NULL, * bf_buf = NULL;
    size_t ef_size = file_size(envfile);
    size_t bf_size = file_size(blacklistfile)+3;
    int ret = -1;
    int readed = 0;
        
    ef_buf = (char*)malloc(ef_size);
    if(!ef_buf) return -1;
    bf_buf = (char*)malloc(bf_size);
    if(!bf_buf){
        free(ef_buf);
        return -1;
    }
    
    memset(ef_buf, 0, ef_size);
    //blacklist last 3 bytes are '*', which make uboot aware of the end of blacklist
    memset(bf_buf, '*', bf_size);    
    
    ret=-2;
    ef = fopen(envfile, "rb");
    if(ef == NULL) goto END;
    bf = fopen(blacklistfile, "rb");
    if(bf == NULL) goto END;
        
    
    ret = -3;
    readed = fread(ef_buf, 1, ef_size, ef);
    if(readed != ef_size) goto END;

    readed = fread(bf_buf, 1, bf_size-3, bf);
    if(readed != bf_size-3) goto END;  

	ret = wfb_update_env(usb, actionlist, ef_buf, ef_size, bf_buf, bf_size);

END:    
    free(ef_buf);
    free(bf_buf);
    if(ef != NULL) fclose(ef);
    if(bf != NULL) fclose(bf);
    
    return ret;
}

static int update_nand_raw(usb_handle *usb, fb_action_list *list, char *ptn, char * file){
    void *content = NULL;
    unsigned sz;
    int status = 0;    
    if(file != NULL){
        content = load_file(file, &sz);
        if(content == NULL){
            printf("file to open %s %s\n", ptn, file);
            perror("reason");
            return -1;
        }
        status = wfb_update_nand(usb, list, ptn, content, sz);
        free(content);
        if(status < 0){
           printf("shit happens while updating %s\n", ptn);   
           return -1;
        }       
    }
    
    return 0;
}



int update_nand_sparse(usb_handle *usb, fb_action_list * list, const char *ptn, const char *filename, int maxsize){
    int64_t sz64;
    int64_t filesize;
    int ret = 0;
    char cmd[64]={0};
	char debugstr[1024]={0};
    FILE *file;
    char * buffer;
    int readed;
    
    filesize = file_size(filename);
    if(filesize < 0){
        OutputDebugString("get filesize fail\n");
		printf("fail to get file size of %s\n", filename);
        return -1;
    }    
    
    buffer = malloc(maxsize);
    if(buffer == NULL){
        printf("malloc fail\n");
        return -1;
    }
    
    file = fopen(filename, "rb");
    if(file == NULL){
       free(buffer);
        OutputDebugString("open file fail\n");	   
       return -1;
    }
    
    ret = wfb_sparse_start(usb, list, filesize);
    if(ret < 0){
        goto END;
    }
    

    while(1){
        readed = fread(buffer, 1, maxsize, file);
        if(readed == maxsize){
            if((ret = wfb_update_nand(usb, list, ptn, buffer, maxsize)) < 0)
               break;
        }else if(feof(file)){
            if(readed > 0)
               ret = wfb_update_nand(usb, list, ptn, buffer, readed);
            break;        
        }else{
            perror("read error");
			OutputDebugString("read error!");
            ret = -100;
            break;
        }
        if(ret < 0) break;
    }
    
    ret |= wfb_sparse_end(usb, list);
    
END:
	sprintf(debugstr, "ret is %d\n", ret);
	OutputDebugString(debugstr);
    free(buffer);      
    fclose(file);
    
    return ret;
}


static int update_nand_yaffs2(usb_handle *usb, fb_action_list *list, char *ptn, char *file, int delete_tmp){
    char respBuf[64]={0};
    int page_size = 8192;
    int oob_size = 64;
    char tmpfilename[1024]={0};
    int status = 0;   
    void *content;
    unsigned sz;
    int max_download_size = 0;
    int64_t filesize = 0;
   
   if(file != NULL){
        char * p, *pend;
        if(wfb_query_ptn_nandinfo(usb, list, ptn, respBuf, sizeof(respBuf)) < 0){
            printf("fail to query nand info of %s\n", ptn);
            return -1;
        }
        
        printf("nand info %s\n", respBuf);
        
        if(!(p = strstr(respBuf, "pagesize="))){
            printf("unknown return value %s\n", respBuf);
            return -1;
        }
        
        page_size = strtol (p+9, &pend, 10);
        if(page_size <= 0){
            printf("fail to retrieve page size : %s, p %s, pend %s\n", respBuf, p, pend);
            return -1;
        }
        
        if(!(p = strstr(respBuf, "oobsize="))){
            printf("unknown return value %s\n", respBuf);
            return -1;
        }
        
        oob_size = strtol (p+8, &pend, 10);
        if(oob_size <= 0){
            printf("fail to retrieve oob size : %s, p %s, pend %s\n", respBuf, p, pend);
            return -1;
        }        
        
        if(wfb_query_max_download_size(usb, list, respBuf, sizeof(respBuf)) < 0){
            printf("fail to query max download size\n");
            return -1;        
        }
        
        printf("max download size is %s\n", respBuf);
        
        max_download_size = strtol(respBuf, &pend, 10);
        
        if(max_download_size <= 0){
            printf("fail to retrieve max download size\n");
            return -1;
        }
        
        if((max_download_size % (page_size + oob_size)) != 0){
            printf("max download size(%d) is not pagesize(%d)+oobsize(%d) aligned\n", max_download_size, page_size, oob_size);
            return -1;
        }
        
        srand(time(NULL));
        snprintf(tmpfilename, sizeof(tmpfilename), "%s_tmp%08x.img", ptn, rand());
        //64 is oobsize
        if(convyaffs2img(file, tmpfilename, page_size, 64, 0) < 0){
            printf("fail to covert %s yaffs2 image\n", file);
            return -1;
        }
        
        filesize = file_size(tmpfilename);
        if(filesize < 0){
            printf("fail to get file size of %s\n", tmpfilename);
            return -1;
        }

        if(filesize <= max_download_size){
            content = load_file(tmpfilename, &sz);
            if(content == NULL){
                printf("fail to open %s\n", tmpfilename);
                perror("reason");
                return -1;
            }
            status = wfb_update_nand(usb, list, ptn, content, sz);
            free(content);
        }else{        
            //make sure update chunk is (page_size+64) align, 64 is oobsize
            status = update_nand_sparse(usb, list, ptn, tmpfilename, max_download_size);
        }
        
        
        if(delete_tmp){
            unlink(tmpfilename);
        }        
        if(status < 0){
           printf("shit happens while updating %s\n", ptn);   
           return -1;         
        }                        
    } 
    
    return 0;
}

int main(int argc, char **argv){

    char *wload = NULL;
    char *uboot = NULL;
    char *boot = NULL;
    char *recovery = NULL;
    char *system = NULL;
    char *data = NULL;
    char *logo = NULL;
    char *serial = NULL;
    int c, list = 0;
    usb_handle * usb = NULL;
    int wants_reboot = 0;
    int status = 0;
    int delete_temp = 0;
    int announce = 1;
    int err = 0;
    fb_action_list *actionlist=NULL;
    char *env = NULL;
    char *blacklist = NULL;


    static struct option long_options[] = {
        {"wload",     required_argument, 0,  'w' },
        {"uboot",     required_argument, 0,  'u' },
        {"boot",      required_argument, 0,  'b' },
        {"recovery",  required_argument, 0,  'r' },
        {"system",    required_argument, 0,  's' },
        {"data",      required_argument, 0,  'd' },        
        {"logo",      required_argument, 0,  'l' },
        {"serial",    required_argument, 0,  'S' },
        {"list",      no_argument,       0,  'L' },
        {"reboot",    no_argument,       0,  'R' },        
        {"help",      no_argument,       0,  'h' },
        {"env",       required_argument, 0,  'e' },
        {"blacklist", required_argument, 0,  'B' },        
        {0,           0,                 0,   0  }
    }; 

    while (1) {
        c = getopt_long(argc, argv, "w:u:b:r:s:d:l:S:hLRDe:B:", &long_options, NULL);
        if (c < 0) {
            break;
        }

        switch (c) {
        case 'w':
            wload = optarg;
            break;
        case 'u':
            uboot = optarg;
            break;
        case 'b':
            boot = optarg;
            break;
        case 'r':
            recovery = optarg;
            break;
        case 's':
            system = optarg;
            break;
        case 'd':
            data = optarg;
            break;
        case 'l':
            logo = optarg;
            break;
        case 'S':
            serial = optarg;
            break;
        case 'L':
            list = 1;
            break;
        case 'R':
            wants_reboot = 1;
            break;
        case 'D':
            delete_temp = 1;
            break;
        case 'e':
            env = optarg;
            break;
        case 'B':
            blacklist = optarg;
            break;
        case 'h':
            usage();
            return 1;
        case '?':
            return 1;
        default:
            abort();
        }
    }
    
    if(list == 1){
        wfb_list_devices(list_devices_callback); 
        return 0;
    }
    
    actionlist = init_fb_action_list();
    if(actionlist == NULL){
        printf("fail to init action list\n");
        return -1;
    }
    
   usb = open_device(serial);
    
    while(usb == NULL) {
        usb = open_device(serial);
        err = GetLastError();
        fprintf(stderr, "errno is %d\n", err);
        if(usb == NULL && announce) {
            announce = 0;
            
            fprintf(stderr,"< waiting for device >\n");
        }
        sleep(1);
    }    
        
    //if(usb == NULL){
    //    printf("fail to open usb device : %s\n", serial != NULL?serial:"");
    //    uninit_fb_action_list(actionlist);
    //    return -1;
    //}
    
    if(wants_reboot == 1){
        wfb_reboot_device(usb, actionlist);
        goto END;
    }
    
    

    status = update_spi(usb, actionlist, "w-load", wload);
    if(status < 0) goto END;    
    
    status = update_spi(usb, actionlist, "u-boot", uboot);
    if(status < 0) goto END;    
    
    status = update_nand_raw(usb, actionlist, "boot", boot);
    if(status < 0) goto END;        

    status = update_nand_raw(usb, actionlist, "recovery", recovery);
    if(status < 0) goto END;            

    status = update_nand_raw(usb, actionlist, "logo", logo);
    if(status < 0) goto END;            
    
    if(env != NULL && blacklist != NULL){
        unsigned short rHiByte, rMdByte, rLoByte;
        char eth[64]={0};
        printf("before update_env %s, %s\n", env, blacklist);
        status = update_env(usb, actionlist, env, blacklist);
        printf("after update env %d\n", status);
        if(status < 0) goto END;
        
        //generate random ethaddr
        srand(time(NULL));
        rHiByte = rand()%0xFFFF;
        rMdByte = rand()%0xFFFF;
        rLoByte = rand()%0xFFFF;
        
        snprintf(eth, sizeof(eth), "%02x:%02x:%02x:%02x:%02x:%02x", rHiByte>>16, rHiByte, rMdByte>>16, rMdByte, rLoByte>>16, rLoByte);
        wfb_set_env(usb, actionlist, "ethaddr", eth);
        if(status < 0) goto END;        
        wfb_save_env(usb, actionlist);
        if(status < 0) goto END;        
    }
    
    status = update_nand_yaffs2(usb, actionlist, "system", system, delete_temp);
    if(status < 0) goto END;            
    
    status = update_nand_yaffs2(usb, actionlist, "data", data, delete_temp);
    if(status < 0) goto END;            
     
    
END:    
    uninit_fb_action_list(actionlist);
    close_device(usb);
    return 0;
}
