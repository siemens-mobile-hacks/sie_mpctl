#include <stdio.h>
#include <swilib.h>
#include <stdint.h>
#include <string.h>
#include <mplayer.h>
#include <nu_swilib.h>

enum {
    CONNECT_STATE_NONE,
    CONNECT_STATE_INITIAL,
    CONNECT_STATE_CONNECT,
    CONNECT_STATE_CONNECTED,
};

int SOCKET;
int DNR_ID;
GBSTMR tmr_connect, tmr_ping;

int PONG;
unsigned int CONNECT_STATE = CONNECT_STATE_NONE;

const int minus11 = -11;
unsigned short maincsm_name_body[140];

void Connect();
void Connect_Proc();
void Reconnect_Proc();

typedef struct {
    CSM_RAM csm;
} MAIN_CSM;

struct {
    char song[256];
    char status;
    uint8_t volume;
    uint8_t muted;
} DATA = {
        "\0",
        0,
        0,
        0,
};

void Connect() {
    if (CONNECT_STATE == CONNECT_STATE_NONE && IsGPRSEnabled()) {
        SOCKET = socket(AF_INET, SOCK_STREAM, 0);
        if (SOCKET != -1) {
            int err = 1;
            int ***pres = NULL;
            SOCK_ADDR sa;
            sa.family = 1;
            sa.port = htons(8989); // thanks Viktor89
            unsigned int errors = 0;
            CONNECT_STATE = CONNECT_STATE_INITIAL;
            while (err) {
                if (errors >= 10) {
                    goto RETRY;
                }
                if (CONNECT_STATE == CONNECT_STATE_NONE) {
                    return;
                }
                err = async_gethostbyname("siempctl.ru", &pres, &DNR_ID);
                NU_Sleep(100);
                errors++;
            }
            if (pres && pres[3]) {
                sa.ip = pres[3][0][0];
            } else {
                goto RETRY;
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

void Ping() {
    static uint8_t p = 0xFF;
    PONG = 0;
    send(SOCKET, &p, sizeof(uint8_t), 0);
}

void SendData() {
    if (!IsPlayerOn()) {
        DATA.status = -1;
    } else {
        DATA.status = GetPlayStatus();
    }
    send(SOCKET, &DATA, sizeof(DATA), 0);
}


void Connect_Proc() {
    SUBPROC(Connect);
}

void Reconnect_Proc() {
    SUBPROC(Reconnect);
}

void Ping_Proc() {
    if (CONNECT_STATE == CONNECT_STATE_CONNECTED) {
        if (!PONG) {
            shutdown(SOCKET, SHUT_RDWR);
            closesocket(SOCKET);
            return;
        }
        SUBPROC(Ping);
    }
    GBS_StartTimerProc(&tmr_ping, 216 * 5, Ping_Proc);
}

void Receive() {
    uint8_t cmd;
    if (CONNECT_STATE == CONNECT_STATE_CONNECTED) {
        if (recv(SOCKET, &cmd, 1, 0)) {
            if (cmd == 0xFF) {
                PONG = 1;
            }
            else {
                if (cmd == PLAYER_PREV || cmd == PLAYER_NEXT || cmd == PLAYER_PLAY) {
                    if (!IsPlayerOn()) {
                        MEDIA_PLAYLAST();
                    }
                }
                Send_MPlayer_Command(cmd, 0);
            }
        }
    }
}

int maincsm_onmessage(CSM_RAM *data, GBS_MSG *msg) {
    if (msg->msg == MSG_HELPER_TRANSLATOR) {
        if ((int)msg->data1 == SOCKET) {
            switch ((int)msg->data0) {
                case ENIP_SOCK_CONNECTED:
                    GBS_DelTimer(&tmr_ping);
                    GBS_DelTimer(&tmr_connect);
                    CONNECT_STATE = CONNECT_STATE_CONNECTED;
                    SUBPROC(Ping);
                    GBS_StartTimerProc(&tmr_ping, 216 * 5, Ping_Proc);
                    ShowMSG(1, (int)"Connected!");
                    break;
                case ENIP_SOCK_DATA_READ:
                    SUBPROC(Receive);
                    break;
                case ENIP_SOCK_CLOSED: case ENIP_SOCK_REMOTE_CLOSED:
                    if (CONNECT_STATE != CONNECT_STATE_NONE) {
                        CONNECT_STATE = CONNECT_STATE_NONE;
                        GBS_DelTimer(&tmr_ping);
                        GBS_DelTimer(&tmr_connect);
                        PONG = 0;
                        GBS_StartTimerProc(&tmr_connect, 216 * 3, Reconnect_Proc);
                    }
                    break;
            }
        }
    }
    return 1;
}

void maincsm_oncreate(CSM_RAM *data) {
    SUBPROC(Connect);
}

void maincsm_onclose(CSM_RAM *csm) {
    GBS_DelTimer(&tmr_ping);
    GBS_DelTimer(&tmr_connect);
    CONNECT_STATE = CONNECT_STATE_NONE;
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
    LockSched();
    save_cmpc = CSM_root()->csm_q->current_msg_processing_csm;
    CSM_root()->csm_q->current_msg_processing_csm = CSM_root()->csm_q->csm.first;
    CreateCSM(&MAINCSM.maincsm,dummy,0);
    CSM_root()->csm_q->current_msg_processing_csm = save_cmpc;
    UnlockSched();
    return 0;
}
