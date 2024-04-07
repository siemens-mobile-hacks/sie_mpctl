#include <stdint.h>
#include <string.h>
#include <swilib.h>
#include <mplayer.h>
#include <xtask_ipc.h>
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

int SOCKET;
int DNR_ID;
GBSTMR TMR;
GBSTMR tmr_connect, tmr_send_data, tmr_send_data_loop;

int PONG;
unsigned int CONNECT_STATE = CONNECT_STATE_NONE;

const int minus11 = -11;
unsigned short maincsm_name_body[140];

void Connect();
void Connect_Proc();
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
} DATA = {
        "\0",
        0,
        0,
        0,
};

unsigned int IsMPOn() {
    return (Sie_CSM_FindByAddr(CFG_MP_CSM_ADDR)) ? 1 : 0;
}

void Connect() {
    if (CONNECT_STATE == CONNECT_STATE_NONE && IsGPRSEnabled()) {
        SOCKET = socket(AF_INET, SOCK_STREAM, 0);
        if (SOCKET != -1) {
            int err = 1;
            int ***pres = NULL;
            SOCK_ADDR sa;
            sa.family = 1;
            sa.port = htons(8989); // thanks Viktor89
            sa.ip = str2ip(CFG_HOST);
            if (sa.ip == 0xFFFFFFFF) { // domain
                unsigned int errors = 0;
                CONNECT_STATE = CONNECT_STATE_INITIAL;
                while (err) {
                    if (errors >= 10) {
                        goto RETRY;
                    }
                    if (CONNECT_STATE == CONNECT_STATE_NONE) {
                        return;
                    }
                    err = async_gethostbyname(CFG_HOST, &pres, &DNR_ID);
                    NU_Sleep(100);
                    errors++;
                }
                if (pres && pres[3]) {
                    sa.ip = pres[3][0][0];
                } else {
                    goto RETRY;
                }
            }
            if (connect(SOCKET, &sa, sizeof(SOCK_ADDR)) != -1) {
                CONNECT_STATE = CONNECT_STATE_CONNECT;
                return;
            } else {
                closesocket(SOCKET);
                return;
            }
        }
    }
    RETRY:
    CONNECT_STATE = CONNECT_STATE_NONE;
    GBS_StartTimerProc(&tmr_connect, 216 * 3, Connect_Proc);
}

void Disconnect() {
    if (SOCKET >= 0) {
        closesocket(SOCKET);
        SOCKET = -1;
    }
    CONNECT_STATE = CONNECT_STATE_NONE;
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

void SendData(int reset_pong) {
    if (reset_pong) {
        PONG = 0;
    }
    send(SOCKET, &DATA, sizeof(DATA), 0);
}


void Connect_Proc() {
    SUBPROC(Connect);
}

void Reconnect_Proc() {
    SUBPROC(Reconnect);
}

void SendData_Proc() {
    SetData();
    SUBPROC(SendData, 0);
}

void SendDataLoop() {
    if (CONNECT_STATE == CONNECT_STATE_CONNECTED) {
        if (!PONG) {
            shutdown(SOCKET, SHUT_RDWR);
            closesocket(SOCKET);
            return;
        }
        DelTimers();
        SetData();
        SUBPROC(SendData, 1);
        GBS_StartTimerProc(&tmr_send_data_loop, 216 * 10, SendDataLoop);
    }
}

void StartTimers() {
    GBS_StartTimerProc(&tmr_send_data, 216 * 2, SendData_Proc);
    GBS_StartTimerProc(&tmr_send_data_loop, 216 * 10, SendDataLoop);
}

void DelTimers() {
    GBS_DelTimer(&tmr_connect);
    GBS_DelTimer(&tmr_send_data);
    GBS_DelTimer(&tmr_send_data_loop);
}

void MediaPlayLastAndLock() {
    static IPC_REQ ipc;
    ipc.name_to = IPC_XTASK_NAME;
    GBS_SendMessage(MMI_CEPID, KEY_DOWN, ENTER_BUTTON);
    MEDIA_PLAYLAST();
    GBS_SendMessage(MMI_CEPID, MSG_IPC, IPC_XTASK_IDLE, &ipc);
    KbdLock();
    DrawScreenSaver();
}

void Receive() {
    uint8_t cmd;
    if (CONNECT_STATE == CONNECT_STATE_CONNECTED) {
        if (recv(SOCKET, &cmd, 1, 0)) {
            PONG = 1;
            if (cmd != 0xFF) { // just ping
                if (cmd == PLAYER_PREV || cmd == PLAYER_NEXT || cmd == PLAYER_PLAY) {
                    if (!IsMPOn()) {
                        static GBSTMR tmr;
                        KbdUnlock();
                        CloseScreensaver();
                        Sie_Exec_Shortcut("MEDIA_PLAYER");
                        GBS_StartTimerProc(&tmr, (long)(216 * 1.5), MediaPlayLastAndLock);
                    }
                }
                Send_MPlayer_Command(cmd, 0);
                DelTimers();
                StartTimers();
            }
        }
    }
}

int maincsm_onmessage(CSM_RAM *data, GBS_MSG *msg) {
    if (msg->msg == MSG_HELPER_TRANSLATOR) {
        if ((int)msg->data1 == SOCKET) {
            switch ((int)msg->data0) {
                case ENIP_SOCK_CONNECTED:
                    CONNECT_STATE = CONNECT_STATE_CONNECTED;
                    DelTimers();
                    StartTimers();
                    ShowMSG(1, (int)"Connected!");
                    break;
                case ENIP_SOCK_DATA_READ:
                    SUBPROC(Receive);
                    break;
                case ENIP_SOCK_REMOTE_CLOSED:
                    SUBPROC(Disconnect);
                    break;
                case ENIP_SOCK_CLOSED:
                    if (CONNECT_STATE != CONNECT_STATE_NONE) {
                        CONNECT_STATE = CONNECT_STATE_NONE;
                        PONG = 0;
                        DelTimers();
                        GBS_StartTimerProc(&tmr_connect, 216 * 3, Reconnect_Proc);
                    }
                    break;
            }
        }
    } else if (msg->msg == MSG_RECONFIGURE_REQ) {
        if (strcmpi(CFG_PATH, (char *)msg->data0) == 0) {
            ShowMSG(1, (int)"SieMPCtl config updated!");
            InitConfig();
            shutdown(SOCKET, SHUT_RDWR);
            closesocket(SOCKET);
        }
    }
    return 1;
}

void maincsm_oncreate(CSM_RAM *data) {
    SUBPROC(Connect);
}

void maincsm_onclose(CSM_RAM *csm) {
    CONNECT_STATE = CONNECT_STATE_NONE;
    DelTimers();
    closesocket(SOCKET);
    SUBPROC((void *)kill_elf);
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
