#include <stdint.h>
#include <string.h>
#include <swilib.h>
#include <mplayer.h>
#include <swilib/nucleus.h>
#include <sie/sie.h>
#include "config_loader.h"

enum {
    CONNECT_STATE_NONE,
    CONNECT_STATE_INITIAL,
    CONNECT_STATE_CONNECT,
    CONNECT_STATE_CONNECTED,
};

extern char CFG_PATH[];
extern char CFG_HOST[];
extern char CFG_MP_CSM_ADDR[];

int SOCKET = -1, PONG = 1;
unsigned int CONNECT_STATE = CONNECT_STATE_NONE;
GBSTMR TMR_CONNECT, TMR_SEND_DATA, TMR_SEND_DATA_LOOP;

const int minus11 = -11;
unsigned short maincsm_name_body[140];

void Connect();
void Reconnect_Proc();
void DelTimers();

typedef struct {
    CSM_RAM csm;
} MAIN_CSM;

struct {
    char track[256];
    char status;
    uint8_t volume;
    uint8_t muted;
} DATA;

unsigned int IsMPOn() {
    return (Sie_CSM_FindByAddr(CFG_MP_CSM_ADDR)) ? 1 : 0;
}

void Disconnect() {
    CONNECT_STATE = CONNECT_STATE_NONE;
    DelTimers();
    if (SOCKET) {
        shutdown(SOCKET, SHUT_RDWR);
        closesocket(SOCKET);
        SOCKET = -1;
    }
}

void Connect() {
    if (CONNECT_STATE == CONNECT_STATE_NONE && IsGPRSEnabled()) {
        SOCKET = socket(AF_INET, SOCK_STREAM, 0);
        if (SOCKET != -1) {
            HOSTENT *h = NULL;
            SOCKADDR_IN sa;
            sa.sin_family = AF_INET;
            sa.sin_port = htons(8989); // thanks Viktor89
            sa.sin_addr.s_addr = str2ip(CFG_HOST);
            if (sa.sin_addr.s_addr == 0xFFFFFFFF) {
                CONNECT_STATE = CONNECT_STATE_INITIAL;
                int dnr_id = 0;
                async_gethostbyname(CFG_HOST, &h, &dnr_id);
            }
            if (h && h->h_addr_list) {
                sa.sin_addr = *(struct in_addr*)h->h_addr_list[0];
                if (connect(SOCKET,     (SOCKADDR*)&sa, sizeof(SOCK_ADDR)) != -1) {
                    CONNECT_STATE = CONNECT_STATE_CONNECT;
                    GBS_StartTimerProc(&TMR_CONNECT, 216 * 10, Reconnect_Proc);
                    return;
                }
            }
        }
    }
    GBS_StartTimerProc(&TMR_CONNECT, 216 * 3, Reconnect_Proc);
}

void Reconnect() {
    Disconnect();
    Connect();
}

void SetData() {
    zeromem(&DATA, sizeof(DATA));
    if (!IsMPOn()) {
        DATA.status = -1;
    } else {
        // track
        WSHDR *dir_ws = (WSHDR*)GetLastAudioTrackDir();
        WSHDR *filename_ws = (WSHDR*)GetLastAudioTrackFilename();

        FILE_PROP file_prop = { 0 };
        file_prop.type = FILE_PROP_TYPE_MUSIC;
        file_prop.filename = AllocWS(256);
        file_prop.tag_title_ws = AllocWS(64);
        file_prop.tag_artist_ws = AllocWS(64);

        wsprintf(file_prop.filename, "%w\\%w", dir_ws, filename_ws);
        if (GetFileProp(&file_prop, filename_ws, dir_ws)) {
            ssize_t len;
            if (wstrlen(file_prop.tag_artist_ws) && wstrlen(file_prop.tag_title_ws)) {
                WSHDR *track = AllocWS(192);
                wsprintf(track, "%w - %w", file_prop.tag_artist_ws, file_prop.tag_title_ws);
                ws_2utf8(track, DATA.track, &len, 255);
                FreeWS(track);
            } else {
                ws_2utf8(filename_ws, DATA.track, &len, 255);
            }
        }
        FreeWS(file_prop.filename);
        FreeWS(file_prop.tag_title_ws);
        FreeWS(file_prop.tag_artist_ws);
        // status
        DATA.status = GetPlayStatus();
    }
}

void Send() {
    if (SOCKET != -1) {
        send(SOCKET, &DATA, sizeof(DATA), 0);
    }
}

void Reconnect_Proc() {
    SUBPROC(Reconnect);
}

void SendData_Proc(){
    SetData();
    SUBPROC(Send);
}

void SendDataLoop() {
    if (!PONG) {
        SUBPROC(Reconnect);
        return;
    }
    if (CONNECT_STATE == CONNECT_STATE_CONNECTED) {
        DelTimers();
        PONG = 0;
        SetData();
        SUBPROC(Send);
        GBS_StartTimerProc(&TMR_SEND_DATA_LOOP, 216 * 5, SendDataLoop);
    }
}

void StartTimers() {
    GBS_StartTimerProc(&TMR_SEND_DATA, 216 * 1, SendData_Proc);
    GBS_StartTimerProc(&TMR_SEND_DATA_LOOP, 216 * 10, SendDataLoop);
}

void DelTimers() {
    GBS_DelTimer(&TMR_CONNECT);
    GBS_DelTimer(&TMR_SEND_DATA);
    GBS_DelTimer(&TMR_SEND_DATA_LOOP);
}

void Receive() {
    if (SOCKET != -1) {
        DelTimers();
        if (CONNECT_STATE == CONNECT_STATE_CONNECTED) {
            char *cmd = malloc(1);
            size_t receive = recv(SOCKET, cmd, 1, 0);
            if (receive != -1) {
                if (*cmd != 0xFF) { // just ping
                    if (*cmd == PLAYER_PREV || *cmd == PLAYER_NEXT || *cmd == PLAYER_PLAY) {
                        if (!IsMPOn()) {
                            KbdUnlock();
                            CloseScreensaver();
                            MediaProc_LaunchLastPlayback();
                            KbdLock();
                            DrawScreenSaver();
                        }
                    }
                    Send_MPlayer_Command(*cmd, 0);
                }
            }
            mfree(cmd);
        }
        StartTimers();
    }
}

int maincsm_onmessage(CSM_RAM *data, GBS_MSG *msg) {
    if (msg->msg == MSG_HELPER_TRANSLATOR) {
        if ((int)msg->data1 == SOCKET) {
            switch ((int)msg->data0) {
                case ENIP_SOCK_CONNECTED:
                    PONG = 1;
                    CONNECT_STATE = CONNECT_STATE_CONNECTED;
                    DelTimers();
                    StartTimers();
                    ShowMSG(1, (int)"SieMPCtl connected to the server");
                    break;
                case ENIP_SOCK_DATA_READ:
                    PONG = 1;
                    SUBPROC(Receive);
                    break;
                case ENIP_SOCK_REMOTE_CLOSED: case ENIP_SOCK_CLOSED:
                    PONG = 0;
                    GBS_StartTimerProc(&TMR_CONNECT, 216 * 3, Reconnect_Proc);
                    break;
            }
        }
    } else if (msg->msg == MSG_RECONFIGURE_REQ) {
        if (strcmpi(CFG_PATH, (char *)msg->data0) == 0) {
            ShowMSG(1, (int)"SieMPCtl config updated!");
            InitConfig();
            SUBPROC(Reconnect);
        }
    }
    return 1;
}

void Close() {
    Disconnect();
    kill_elf();
}

void maincsm_oncreate(CSM_RAM *data) {
    SUBPROC(Connect);
}

void maincsm_onclose(CSM_RAM *csm) {
    SUBPROC(Close)  ;
}

const struct {
    CSM_DESC maincsm;
    WSHDR maincsm_name;
} MAINCSM = {
        {
                maincsm_onmessage,
                maincsm_oncreate,
#ifdef NEWSGOLD
                0,
                0,
                0,
                0,
#endif
                maincsm_onclose,
                sizeof(MAIN_CSM),
                1,
                &minus11
        },
        {
                maincsm_name_body,
                NAMECSM_MAGIC1,
                NAMECSM_MAGIC2,
                0x0,
                139,
                0
        }
};

void UpdateCSMname(void) {
    wsprintf((WSHDR *)(&MAINCSM.maincsm_name), "%s", "SieMPCtl")
}

int main() {
    CSM_RAM *save_cmpc;
    char dummy[sizeof(MAIN_CSM)];
    UpdateCSMname();
    InitConfig();
    LockSched();
    save_cmpc = CSM_root()->csm_q->current_msg_processing_csm;
    CSM_root()->csm_q->current_msg_processing_csm = CSM_root()->csm_q->csm.first;
    CreateCSM(&MAINCSM.maincsm,dummy,0);
    CSM_root()->csm_q->current_msg_processing_csm = save_cmpc;
    UnlockSched();
    return 0;
}
