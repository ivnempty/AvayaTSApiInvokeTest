#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <windows.h>
#include <pthread.h>

#include "log.h"
#include "acs.h"
#include "attpriv.h"
#include "csta.h"
#include "cstadefs.h"
#include "CSTA_device_feature_types.h"

#define INVOKEID_TYPE	LIB_GEN_ID
#define INVOKEID		0
#define STREAM_TYPE     ST_CSTA
#define LEVELREQ		ACS_LEVEL1
#define APIVER			"TS2"
#define SENDQSIZE       0
#define SENDEXTRABUFS   0
#define RECVQSIZE       0
#define RECVEXTRABUFS   0
#define APPNAME         ""

typedef int (*FuncPtr)(const char *, const char *, const char *, int , const char *, const char *, const char *, const char *, const char *);

typedef struct NewLogin_e {
    char *serverid;
    char *login;
    char *pwd;
    char *deviceId;
    char *target_deviceId;
    char *uuinfo;
    int timeout;
} NewLogin_e;

void open_stream(char *, char *, char *);
void snapshot_device(char *);
void consult_call(char *, char *);
void close_stream(void);
void *event_poll_thread(void *);
void mainfunc(NewLogin_e newlogin);
void *event_thread2(void *argu);
void *mon_call_thread(void *argu);

void SleepTest()
{
	printf("begin sleep 2 sec\n");
	Sleep(2000); //from "windows.h"
	printf("end\n");

	time_t now = time(0);
	printf("%s\n", ctime(&now));
	struct tm *local = localtime(&now);
	printf("Today: %02d/%02d/%02d %02d:%02d:%02d\n", local->tm_year + 1900, local->tm_mon + 1, local->tm_mday, local->tm_hour, local->tm_min, local->tm_sec);

	Sleep(2000);
	printf("%ld\n", time(0) - now);
}

void LogTest()
{
    char *serverid = "AVAYA#CM6#CSTA#AES-VM-01", *login = "cti", *pwd = "itapps", *deviceId = "6005", *destDevId = "6007", *uuinfo = "UUInfo Value";
	short timeout = 60;
    char *logpath = "D:\\Development\\Other\\CallDLL\\bin\\Debug", *filename = "dlllog";

	HINSTANCE dll = LoadLibrary(TEXT("test_dll.dll\n"));
	if (dll != NULL)
	{
		FuncPtr dllFunc = (FuncPtr)GetProcAddress(dll, "doTest\n");
		if (dllFunc != NULL)
		{
			int ret = dllFunc(serverid, login, pwd, timeout, uuinfo, deviceId, destDevId, logpath, filename);
			printf("dll call result: %d\n", ret);

		}
		FreeLibrary(dll);
	}
}

typedef enum State_e
{
    NONE, OPEN_STREAM, SNAPSHOT_DEVICE, CONSULTATION_CALL, TRANSFERRED, FAILURE
} State_e;

State_e state;
pthread_t thread, thread2, thread3;
int status, status2;
short end_thread_flag;

ACSHandle_t acsHandle, acsHandle2;
RetCode_t rc, rc2;
ConnectionID_t *conns = NULL;
int new_call_id;

void open_stream(char *serverid, char *login, char *pwd)
{
    printf("open stream\n");
    printf("%s %s %s", serverid, login, pwd);

    if (state == NONE)
    {
        ATTPrivateData_t privateData;
        strcpy(privateData.vendor, "VERSION");
        privateData.data[0] = PRIVATE_DATA_ENCODING;
        attMakeVersionString("2-9", &(privateData.data[1]));
        privateData.length= strlen( &privateData.data[1]) + 2;

        rc = acsOpenStream(&acsHandle, INVOKEID_TYPE, INVOKEID, STREAM_TYPE,
                          (ServerID_t *)serverid, (LoginID_t *)login, (Passwd_t *)pwd, (AppName_t *)APPNAME, LEVELREQ,
                          (Version_t *)APIVER, SENDQSIZE, SENDEXTRABUFS, RECVQSIZE, RECVEXTRABUFS, (PrivateData_t *)&privateData);
        printf("acsOpenStream returns %d\n", rc);

        status = pthread_create(&thread, NULL, event_poll_thread, NULL);
        printf("pthread status is %d\n",status);
        if (status != 0) {
          printf("Creation poll_thread failed\n");
        }
    }
}

void snapshot_device(char *deviceId)
{
    printf("snapshot device\n");
    if (state == OPEN_STREAM)
    {
        rc = cstaSnapshotDeviceReq(acsHandle,0,(DeviceID_t *)deviceId, NULL);
        printf("cstaSnapshotDeviceReq returns %d\n", rc);
    }
}

void consult_call(char *target_deviceId, char *uuinfo)
{
    printf("consult call\n");
    if (state == SNAPSHOT_DEVICE)
    {
        //compose uui info & do consultation call
        ATTPrivateData_t priData;
        ATTV5UserToUserInfo_t attUUInfo;
        attUUInfo.type = UUI_IA5_ASCII;
        attUUInfo.data.length = strlen(uuinfo) + 1;
        memcpy(attUUInfo.data.value, uuinfo, strlen(uuinfo));
        printf("UUInfo = %s, length = %d\n", attUUInfo.data.value, attUUInfo.data.length);

        //do attConsultation call
        rc = attConsultationCall(&priData, NULL, FALSE, &attUUInfo);
        if (rc != ACSPOSITIVE_ACK)
        {
            printf("attConsultationCall returns : %d\n", rc);
            return;
        }

        rc = cstaConsultationCall(acsHandle, INVOKEID, &conns[0], (DeviceID_t *)target_deviceId, (PrivateData_t *)&priData);
        printf("cstaSnapshotDeviceReq returns %d\n", rc);
    }
}

void make_call(char *deviceId, char *target_deviceId, char *uuinfo)
{
    printf("make call\n");
    if (state == SNAPSHOT_DEVICE)
    {
        //compose uui info & do consultation call
        ATTPrivateData_t priData;

        ATTUserToUserInfo_t attUUInfo;
        attUUInfo.type = UUI_IA5_ASCII;
        attUUInfo.data.length = strlen(uuinfo);
        strcpy((char *)attUUInfo.data.value, uuinfo);

        printf("UUInfo = %s, length = %d\n", attUUInfo.data.value, attUUInfo.data.length);

        //do make call
        rc = attV6MakeCall(&priData, NULL, FALSE, &attUUInfo);
        printf("attMakeCall returns : %d\n", rc);
        if (rc != ACSPOSITIVE_ACK)
        {
            return;
        }

        printf("\nPrivate Data: vender %s, length %d\n", priData.vendor, priData.length);
        for (int i=0; i<priData.length; i++) printf("%x ", priData.data[i]);
        printf("\n");

        rc = cstaMakeCall(acsHandle, INVOKEID, (DeviceID_t *)deviceId, (DeviceID_t *)target_deviceId, (PrivateData_t *)&priData);
        printf("cstaMakeCall returns %d\n", rc);
    }
}

void close_stream(void)
{
    printf("close stream\n");
    if (state != NONE)
    {
        rc = acsCloseStream(acsHandle, 0, NULL);
        printf("acsOCloseStream returns %d\n", rc);
        state = NONE;
    }
}

void *event_poll_thread(void *argu)
{
    CSTAEvent_t ce;
    unsigned short size = sizeof(ce), queued;
    end_thread_flag = FALSE;
    ATTPrivateData_t priData;
    priData.length = ATT_MAX_PRIVATE_DATA;

    while (!end_thread_flag)
    {
        Sleep(500); //sleep half a second
        rc = acsGetEventPoll(acsHandle, &ce, &size, (PrivateData_t *)&priData, &queued);
        printf("acsGetEventPoll returns %d\n", rc);

        switch (state)
        {
        case NONE:
            printf("NONE -> OPEN_STREAM\n");
            if (rc == ACSPOSITIVE_ACK)
            {
                printf("Class: %d, type: %d\n", ce.eventHeader.eventClass, ce.eventHeader.eventType);
                if (ce.eventHeader.eventClass == ACSCONFIRMATION)
                {
                    if (ce.eventHeader.eventType == ACS_OPEN_STREAM_CONF)
                    {
                        printf("Stream opening sucessfully\n");
                        printf("    API Version : %s\n", ce.event.acsConfirmation.u.acsopen.apiVer);
                        printf("    Library Version : %s\n", ce.event.acsConfirmation.u.acsopen.libVer);
                        printf("    TSERVER Version : %s\n", ce.event.acsConfirmation.u.acsopen.tsrvVer);
                        printf("    Driver Version : %s\n", ce.event.acsConfirmation.u.acsopen.drvrVer);
                        printf("    Private Data Length : %d\n", priData.length);
                        state = OPEN_STREAM;
                    }
                    else
                    {
                        end_thread_flag = TRUE;
                    }
                }
            }
            break;
        case OPEN_STREAM:
            printf("OPEN_STREAM -> SNAPSHOT_DEVICE\n");
            if (rc == ACSPOSITIVE_ACK)
            {
                printf("Class: %d, type: %d\n", ce.eventHeader.eventClass, ce.eventHeader.eventType);
                if (ce.eventHeader.eventClass == CSTACONFIRMATION)
                {
                    if (ce.eventHeader.eventType == CSTA_SNAPSHOT_DEVICE_CONF)
                    {
                        printf("Snapshot device sucessfully\n");

                        CSTASnapshotDeviceData_t snapshotData = ce.event.cstaConfirmation.u.snapshotDevice.snapshotData;
                        int callCount = snapshotData.count;

                        printf("Device Call Count = %d\n", callCount);
                        if (callCount > 0)
                        {
                            conns = (ConnectionID_t *)malloc(callCount * sizeof(ConnectionID_t));
                            for (int i = 0; i < callCount; i++)
                            {
                                conns[i] = snapshotData.info[i].callIdentifier;
                                printf("snapshot device callid %d = %ld\n", i, conns[i].callID);
                            }
                        }

                        state = SNAPSHOT_DEVICE;
                    }
                    else if (ce.eventHeader.eventType == CSTA_MONITOR_CONF)
                    {
                        printf("Monitor device sucessfully\n");
                    }
                }
            }
            break;
        case SNAPSHOT_DEVICE:
            printf("SNAPSHOT_DEVICE -> CONSULTATION_CALL\n");
            if (rc == ACSPOSITIVE_ACK)
            {
                printf("Class: %d, type: %d\n", ce.eventHeader.eventClass, ce.eventHeader.eventType);
                if (ce.eventHeader.eventClass == CSTACONFIRMATION)
                {
                    if (ce.eventHeader.eventType == CSTA_CONSULTATION_CALL_CONF ||
                        ce.eventHeader.eventType == CSTA_MAKE_CALL_CONF)
                    {
                        printf("Consult call sucessfully\n");
                        state = CONSULTATION_CALL;

                        new_call_id = ce.event.cstaConfirmation.u.consultationCall.newCall.callID;
                        printf("New Call ID: %d, Device ID: %s", new_call_id, ce.event.cstaConfirmation.u.consultationCall.newCall.deviceID);
                    }
                }
            }
            break;
        case CONSULTATION_CALL:
            printf("CONSULTATION_CALL -> TRANSFERRED");
            if (rc == ACSPOSITIVE_ACK)
            {
                printf("Class: %d, type: %d", ce.eventHeader.eventClass, ce.eventHeader.eventType);
                if (ce.eventHeader.eventClass == CSTACONFIRMATION)
                {
                    switch (ce.eventHeader.eventType)
                     {
                     case CSTA_DELIVERED:
                         printf("=== CSTA_DELIVERED\n");
                         ATTEvent_t attEvt;
                         rc = attPrivateData(&priData, &attEvt);
                         printf("Caller UUI : %s\n", attEvt.u.deliveredEvent.userInfo.data.value);
                        break;
                    case CSTA_ESTABLISHED:
                        printf("=== CSTA_ESTABLISHED\n");
                        state = TRANSFERRED;
                        break;
                    case CSTA_CONNECTION_CLEARED:
                        printf("=== CSTA_CONNECTION_CLEARED\n");
                        end_thread_flag = TRUE;
                        break;
                     }
                }
            }
            break;
        case FAILURE:
        case TRANSFERRED:
            end_thread_flag = TRUE;
            break;
        }
    }

	printf("exit event poll thread\n");
    pthread_exit(NULL);
    return NULL;
}

void *event_thread2(void *argu)
{
    CSTAEvent_t ce;
    unsigned short size = sizeof(ce);

    ATTPrivateData_t pd;
    pd.length = ATT_MAX_PRIVATE_DATA;

    short exit = FALSE;

    while (!exit)
    {
        Sleep(500);
        rc2 = acsGetEventPoll(acsHandle2, (void *)&ce, &size, (PrivateData_t *)&pd, NULL);
        printf("acsGetEventPoll 2 returns %d\n", rc2);
        printf("\npd length: %d\n\n", pd.length);
        if (rc2 != ACSPOSITIVE_ACK) continue;

        printf("Thread 2 Class: %d, type: %d\n", ce.eventHeader.eventClass, ce.eventHeader.eventType);
        if (ce.eventHeader.eventClass == ACSCONFIRMATION)
        {
            if (ce.eventHeader.eventType == ACS_OPEN_STREAM_CONF)
            {
                printf("Stream opening 2 sucessfully\n");
                state = OPEN_STREAM;
            }
            else
            {
                exit = TRUE;
            }
        }
        else if (ce.eventHeader.eventClass == CSTACONFIRMATION)
        {
             printf(">>>>>>>>> CSTACONFIRMATION\n");
             if (ce.eventHeader.eventType == CSTA_MONITOR_CONF)
            {
                printf(">>>>>>>>>  Monitor device sucessfully\n");
                //ce.event.cstaConfirmation.u.monitorStart.monitorCrossRefID
            }
        }
        else if (ce.eventHeader.eventClass == CSTAUNSOLICITED)
        {


            switch (ce.eventHeader.eventType)
             {
             case CSTA_DELIVERED:
                 printf("=== CSTA_DELIVERED\n");
                 ATTEvent_t attEvt;
                 rc = attPrivateData(&pd, &attEvt);

                 printf(">>> AttEvt Data : length = %d\n", attEvt.u.deliveredEvent.userInfo.data.length);
                 printf(">>> Private Data : length = %d\n", pd.length);

                 printf("UUI ~~~ %s\n", attEvt.u.deliveredEvent.userInfo.data.value);
                break;
            case CSTA_ESTABLISHED:
                printf("=== CSTA_ESTABLISHED\n");
                break;
            case CSTA_CONNECTION_CLEARED:
                printf("=== CSTA_CONNECTION_CLEARED\n");
                exit = TRUE;
                break;
             }
        }
    }

	printf("exit event thread 2\n");
    pthread_exit(NULL);
    return NULL;
}

void *mon_call_thread(void *argu)
{
    NewLogin_e *newlogin = (NewLogin_e *)argu;

    printf("monitor device thread\n");

    ATTPrivateData_t pd;
    strcpy(pd.vendor, "VERSION");
    pd.data[0] = PRIVATE_DATA_ENCODING;
    attMakeVersionString("2-9", &(pd.data[1]));
    pd.length= strlen( &pd.data[1]) + 2;

    rc2 = acsOpenStream(&acsHandle2, INVOKEID_TYPE, INVOKEID, STREAM_TYPE,
                      (ServerID_t *)newlogin->serverid, (LoginID_t *)newlogin->login, (Passwd_t *)newlogin->pwd, (AppName_t *)APPNAME, LEVELREQ,
                      (Version_t *)APIVER, SENDQSIZE, SENDEXTRABUFS, RECVQSIZE, RECVEXTRABUFS, (PrivateData_t *)&pd);
    printf("acsOpenStream 2 returns %d\n", rc2);

    status2 = pthread_create(&thread2, NULL, event_thread2, NULL);
    printf("pthread status 2 is %d\n",status);
    if (status2 != 0) {
      printf("Creation event_thread2 failed\n");
    }

    CSTAMonitorFilter_t filter;
    memset(&filter, 0, sizeof(CSTAMonitorFilter_t));

    time_t startTime, endTime;
    startTime = time(0);
    while (state == NONE)
    {
        Sleep(500);
        endTime = time(0);
        if (endTime - startTime > newlogin->timeout)
            break;
    }
    if (state != OPEN_STREAM)
    {
        printf("Failed to open stream 2: Timeout\n");
        state = FAILURE;
    }

    if (state != FAILURE)
    {
        PrivateData_t pd2;
        strcpy(pd2.vendor, "ECS");
        pd2.length = 0;

        rc2 = cstaMonitorDevice(acsHandle2, INVOKEID, (DeviceID_t *)newlogin->target_deviceId, &filter, &pd2);
        printf("cstaMonitorDevice returns %d\n", rc2);
    }

    return NULL;
}

void mainfunc(NewLogin_e newlogin)
{
    //set timeout
    time_t startTime, endTime;

    //open stream, and create new thread for get event
    open_stream(newlogin.serverid, newlogin.login, newlogin.pwd);
    startTime = time(0);
    while (state == NONE)
    {
        Sleep(500);
        endTime = time(0);
        if (endTime - startTime > newlogin.timeout)
            break;
    }
    if (state != OPEN_STREAM)
    {
        printf("Failed to open stream: Timeout\n");
        state = FAILURE;
    }

    if (state != FAILURE)
    {
        //snapshot device
        snapshot_device(newlogin.deviceId);
        startTime = time(0);
        while (state == OPEN_STREAM)
        {
            Sleep(500);
            endTime = time(0);
            if (endTime - startTime > newlogin.timeout)
                break;
        }
        if (state != SNAPSHOT_DEVICE)
        {
            printf("Failed to snapshot device: Timeout\n");
            state = FAILURE;
        }
        else
        {
            if (conns == NULL)
            {
                printf("Failed to snapshot device: No Connection\n");
                //state = FAILURE;
            }
        }
    }

    if (state != FAILURE)
    {
        //consultation call
        //consult_call(target_deviceId, uuinfo);
        make_call(newlogin.deviceId, newlogin.target_deviceId, newlogin.uuinfo);
        startTime = time(0);
        while (state == SNAPSHOT_DEVICE)
        {
            Sleep(500);
            endTime = time(0);
            if (endTime - startTime > newlogin.timeout)
                break;
        }
        if (state != CONSULTATION_CALL)
        {
            printf("Failed to consult call: Timeout\n");
            state = FAILURE;
        }
    }


    if (state != FAILURE)
    {
        //get transferred event
        startTime = time(0);
        while (state == CONSULTATION_CALL)
        {
            Sleep(500);
            endTime = time(0);
            if (endTime - startTime > newlogin.timeout)
                break;
        }
        if (state != TRANSFERRED)
        {
            printf("Failed to get TRANSFERRED event: Timeout\n");
            state = FAILURE;
        }
    }

    close_stream();
    free(conns);
    conns = NULL;

	end_thread_flag = TRUE;
	printf("Kill event poll thread\n");
    pthread_kill(thread, 0);
    printf("Wait event poll thread to end\n");
    pthread_join(thread, NULL);

}

void mainfunc2(void)
{
    char *serverid = "AVAYA#CM6#CSTA#AES-VM-01\n", *login = "cti\n", *pwd = "itapps\n", *appname = "";
    char *deviceid = "6005\n", *uuinfo = "testABC\n", *transno = "6007";

    //Main work
    State_e state = NONE;
    CSTAEvent_t	eventBuffer;
    unsigned short	eventBufferSize = sizeof(CSTAEvent_t);;
    unsigned short	numEvents;

    //Open Stream
    ACSHandle_t acsHandle;

    ATTPrivateData_t privateData;
    strcpy(privateData.vendor, "VERSION");
    privateData.data[0] = PRIVATE_DATA_ENCODING;
    attMakeVersionString("2-9", &(privateData.data[1]));
    privateData.length= strlen( &privateData.data[1]) + 2;

	RetCode_t rc = acsOpenStream(&acsHandle, INVOKEID_TYPE, INVOKEID, STREAM_TYPE,
                              (ServerID_t *)serverid, (LoginID_t *)login, (Passwd_t *)pwd, (AppName_t *)appname, LEVELREQ,
                              (Version_t *)APIVER, SENDQSIZE, SENDEXTRABUFS, RECVQSIZE, RECVEXTRABUFS, (PrivateData_t *)&privateData);
    if (rc < 0)
    {
        printf("acsOpenStream Failure\n");
    }
    else
    {
        printf("acsOpenStream Success\n");

        rc = acsGetEventBlock(acsHandle, &eventBuffer, &eventBufferSize, (PrivateData_t *)&privateData, &numEvents);
        if (rc == ACSPOSITIVE_ACK)
        {
            if (eventBuffer.eventHeader.eventClass == ACSCONFIRMATION &&
                eventBuffer.eventHeader.eventType == ACS_OPEN_STREAM_CONF)
            {
                printf("Stream open successfully\n");
                state = OPEN_STREAM;
            }
            else
            {
                printf("Stream open failed\n");
            }
        }
    }

    ConnectionID_t *conns;
    if (state == OPEN_STREAM)
    {
        printf("Snapshot Device\n");
        rc = cstaSnapshotDeviceReq(acsHandle, 0, (DeviceID_t *)deviceid, NULL);
        printf("cstaSnapshotDeviceReq returns %d\n", rc);

        rc = acsGetEventBlock(acsHandle, &eventBuffer, &eventBufferSize, (PrivateData_t *)&privateData, &numEvents);
        if (rc == ACSPOSITIVE_ACK)
        {
            if (eventBuffer.eventHeader.eventClass == CSTACONFIRMATION &&
                eventBuffer.eventHeader.eventType == CSTA_SNAPSHOT_DEVICE_CONF)
            {
                printf("Snapshot device success\n");
                CSTASnapshotDeviceData_t snapshotData = eventBuffer.event.cstaConfirmation.u.snapshotDevice.snapshotData;
                int callCount = snapshotData.count;
                CSTASnapshotDeviceResponseInfo_t *devRespInfo = snapshotData.info;
                conns = (ConnectionID_t *)malloc(callCount * sizeof(ConnectionID_t));
                for (int i = 0; i < callCount; i++)
                {
                    conns[i] = devRespInfo[i].callIdentifier;
                }

                state = SNAPSHOT_DEVICE;
            }
            else
            {
                printf("Snapshot device failed\n");
            }
        }
    }

    if (state == SNAPSHOT_DEVICE)
    {
        printf("Set UUInfo\n");

        ATTV5UserToUserInfo_t attUUInfo;
        attUUInfo.type = UUI_IA5_ASCII;
        strcpy((char *)attUUInfo.data.value, (const char *)uuinfo);
        attUUInfo.data.length = strlen((const char *)attUUInfo.data.value);
        printf("\nUUInfo = %s, length = %d\n", attUUInfo.data.value, attUUInfo.data.length);

        rc = attConsultationCall(NULL, NULL, FALSE, &attUUInfo);
        printf("attConsultationCall returns %d\n", rc);
        if (rc != ACSPOSITIVE_ACK)
        {
            printf("attConsultationCall failed\n");
            state = FAILURE;
        }

        if (state == FAILURE)
        {
            printf("Consultation Call\n");

            PrivateData_t priData;
            strcpy(priData.data, '0' + (const char *) uuinfo);
            strcpy(priData.vendor, "VERSION\n");
            priData.length = strlen(priData.data);
            printf("Private Data - Vendor = %s, Data = %s, Length = %d\n", priData.vendor, priData.data, priData.length);

            rc = cstaConsultationCall(acsHandle, INVOKEID, &conns[0], (DeviceID_t *)transno, &priData);
            printf("cstaConsultationCall returns %d\n", rc);

            rc = acsGetEventBlock(acsHandle, &eventBuffer, &eventBufferSize, (PrivateData_t *)&privateData, &numEvents);
            if (rc == ACSPOSITIVE_ACK)
            {
                if (eventBuffer.eventHeader.eventClass == CSTACONFIRMATION &&
                    eventBuffer.eventHeader.eventType == CSTA_CONSULTATION_CALL_CONF)
                {
                    printf("Consultation Call success\n");

                    short exit = FALSE;
                    while (!exit)
                    {
                        //loop to get call transferred event
                        rc = acsGetEventPoll(acsHandle, &eventBuffer, &eventBufferSize, (PrivateData_t *)&privateData, &numEvents);
                        if (eventBuffer.eventHeader.eventType == CSTA_TRANSFERRED)
                        {
                            exit = TRUE;
                        }
                    }

                    state = TRANSFERRED;
                }
                else
                {
                    printf("Consultation Call failed\n");
                }
            }
        }
    }

    if (state != NONE)
    {
        printf("Close Stream\n");
        rc = acsCloseStream(acsHandle, 0, NULL);
        printf("acsCloseStream returns %d\n", rc);
        state = NONE;
    }
}

int main()
{
    //custom logic here
    //mainfunc2();

    char *serverid = "AVAYA#CM6#CSTA#AES-VM-01", *login = "cti", *pwd = "itapps";
    char *deviceid = "6005", *uuinfo = "=== testABC", *transno = "6007";

    NewLogin_e newlogin = {serverid, login, pwd, deviceid, transno, uuinfo, 10};

    pthread_create(&thread3, NULL, mon_call_thread, &newlogin);

    mainfunc(newlogin);

    printf("\nPress \"Enter\" key to exit...\n");
    getchar();
    return 0;
}
