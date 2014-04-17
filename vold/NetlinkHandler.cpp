/*
 * Copyright (C) 2008 The Android Open Source Project
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
#include <errno.h>
#include <string.h>

#define LOG_TAG "Vold"

#include <cutils/log.h>

#include <sysutils/NetlinkEvent.h>
#include "NetlinkHandler.h"
#include "VolumeManager.h"
//#include <utils/wmt_battery.h>
int max_power[5];  //i dont know it is 0~4 or 1~4

NetlinkHandler::NetlinkHandler(int listenerSocket) :
                NetlinkListener(listenerSocket) {
}

NetlinkHandler::~NetlinkHandler() {
}

int NetlinkHandler::start() {
    return this->startListener();
}

int NetlinkHandler::stop() {
    return this->stopListener();
}


int NetlinkHandler::path2host(const char * path)
{
	char * p;
	int host;
	if(!path)
		return -1;
	p = strstr(path,"usb");
	p+=strlen("usb");
	
	if(!p)
		return -1;
	host = *p - '0';
	if(host>=0&&host<=4)
		return host;
	else
		return -1;
	
}

int NetlinkHandler::path2power(const  char * path)
{
	char bmaxpower_file[128];
	char str[128];
	int power;
	FILE * fp;
	str[0] = 0;
	memcpy(str,path,strlen(EXAMPLE_PATH));
	str[strlen(EXAMPLE_PATH)] = 0;
	
	
	sprintf(bmaxpower_file,"/sys%s/bMaxPower",str);

	fp = fopen(bmaxpower_file,"r");
	if(!fp)
		return -1;

	fscanf(fp,"%dmA",&power);


	if(fp)
		fclose(fp);
	
	return power;
}

void NetlinkHandler::send2batterycal(void)
{
	int i;
	int power=0;
	for(i=0;i<5;i++){
		power+=max_power[i];

		
	}

	if(power>2000)
		power = 2000;
	power/=20;
	//SLOGW("call reportModuleChangeEventForBattery %d\n",power);
	//reportModuleChangeEventForBattery(BM_USBDEVICE,power);

}
void NetlinkHandler::process_usb_event(const char * path,int action) 
{
    /*usb path like :"/devices/pci0000:00/0000:00:04.0/usb1/1-1",find "/usb" then you know it is host 2.
 the power file is /sys/devices/pci0000:00/0000:00:04.0/usb1/1-1/bMaxPower */
	int host;
	int power;


	host = path2host(path);

	if (action == NetlinkEvent::NlActionAdd) {
		power = path2power(path);
		if(power<=0)
			return;		
		if(max_power[host]!=power){
			if(max_power[host]!=0){
			}
			max_power[host]=power;
			send2batterycal();
		}
	} else if (action == NetlinkEvent::NlActionRemove) {
		if(max_power[host]!=0){
			max_power[host]=0;
			send2batterycal();
		}
	}

	return;
}


void NetlinkHandler::onEvent(NetlinkEvent *evt) {
    VolumeManager *vm = VolumeManager::Instance();
    const char *subsys = evt->getSubsystem();

    const char *dp = evt->findParam("DEVPATH");
	int action = evt->getAction();

	
	if(strstr(dp,"/devices/pci0000:00")){

		process_usb_event(dp,action);




	}	

    if (!subsys) {
        SLOGW("No subsystem found in netlink event");
        return;
    }

    if (!strcmp(subsys, "block")) {
        vm->handleBlockEvent(evt);
    }
}
