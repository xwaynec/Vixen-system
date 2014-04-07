/*
**
** Copyright 2008, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License"); 
** you may not use this file except in compliance with the License. 
** You may obtain a copy of the License at 
**
**     http://www.apache.org/licenses/LICENSE-2.0 
**
** Unless required by applicable law or agreed to in writing, software 
** distributed under the License is distributed on an "AS IS" BASIS, 
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
** See the License for the specific language governing permissions and 
** limitations under the License.
*/

#define LOG_TAG "su"
#define SOCKET_NAME "su-wmt"
#define SU_YES "y"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>

#include <unistd.h>
#include <time.h>

#include <pwd.h>

#include <private/android_filesystem_config.h>
#include <cutils/properties.h>

#include <sys/socket.h>
#include <cutils/sockets.h>

/*
 * SU can be given a specific command to exec. UID _must_ be
 * specified for this (ie argc => 3).
 *
 * Usage:
 * su 1000
 * su 1000 ls -l
 */
int main(int argc, char **argv)
{
    struct passwd *pw;
    int uid, gid, myuid;
	
	char value[PROPERTY_VALUE_MAX];
    size_t i;
	
	int fd;
	char str[20];
	char sendStr[20];

    myuid = getuid();

    if(argc < 2) {
        uid = gid = 0;
    } else {
        pw = getpwnam(argv[1]);

        if(pw == 0) {
            uid = gid = atoi(argv[1]);
        } else {
            uid = pw->pw_uid;
            gid = pw->pw_gid;
        }
    }
	
    static const char * props[] = {
        "debug.su",
        "debug.su.force",
        "persist.debug.su",
    };

    for(i = 0; i < sizeof(props) / sizeof(props[0]); i++){
        property_get(props[i], value, "0");
//        printf("check %s\n", props[i]);
        if(!strcmp(value, "1")){
            i = 888;
            break;
        }
    }
    
	if(i == 888){
		if(setgid(gid) || setuid(uid)) {
			fprintf(stderr,"su: permission denied\n");
			return 1;
		}
		fprintf(stderr,"su: su successfully\n");
	}else{
	
		fd = socket_local_client(SOCKET_NAME, ANDROID_SOCKET_NAMESPACE_ABSTRACT, SOCK_STREAM);

		if(fd < 0){
			fprintf(stderr,"su: uid %d can not connent to SuService\n", myuid);
			return 1;
		}

		sprintf(sendStr, "%d\n", myuid);

		if(write(fd, sendStr, strlen(sendStr)) == -1){
			close(fd);
			fprintf(stderr,"su: uid %d write error\n", myuid);
			return 1;
		}

		if(read(fd, str, 1) == -1){
			close(fd);
			fprintf(stderr,"su: uid %d read error\n", myuid);
			return 1;
		}

		if(str[0] == 'y'){
			close(fd);
			if(setgid(gid) || setuid(uid)) {
				fprintf(stderr,"su: permission denied\n");
				return 1;
			}
			fprintf(stderr,"su: su successfully\n", myuid);
		}
		else{
			close(fd);
			fprintf(stderr,"su: uid %d not allowed to su\n", myuid);
			return 1;
		}
	}

    /* User specified command for exec. */
    if (argc == 3 ) {
        if (execlp(argv[2], argv[2], NULL) < 0) {
            fprintf(stderr, "su: exec failed for %s Error:%s\n", argv[2],
                    strerror(errno));
            return -errno;
        }
    } else if (argc > 3) {
        /* Copy the rest of the args from main. */
        char *exec_args[argc - 1];
        memset(exec_args, 0, sizeof(exec_args));
        memcpy(exec_args, &argv[2], sizeof(exec_args));
        if (execvp(argv[2], exec_args) < 0) {
            fprintf(stderr, "su: exec failed for %s Error:%s\n", argv[2],
                    strerror(errno));
            return -errno;
        }
    }

    /* Default exec shell. */
	setenv("PS1", "\\w \\$ ", 1);
	setenv("LS_COLORS", "none", 1);

	//perfer busybox sh
    execlp("/bin/sh", "sh", NULL);
	
	//fallback android default sh
	execlp("/system/bin/sh", "sh", NULL);

    fprintf(stderr, "su: exec failed\n");
    return 1;
}

