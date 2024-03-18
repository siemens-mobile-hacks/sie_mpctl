#include <swilib.h>
#include <stdint.h>
#include <mplayer.h>

int SOCKET;
int CONNECT_STATE;

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
    static GBSTMR tmr;
    SOCKET = socket(1, 1, 0);
    if (SOCKET != -1) {
        int err = 1;
        int ***pres = NULL;
        static int DNR_ID = 0;
        SOCK_ADDR sa;
        sa.family = 1;
        sa.port = htons(8989); // thanks Viktor89
        while (err) {
            err = async_gethostbyname("siempctl.ru", &pres, &DNR_ID);
        }
        if (pres && pres[3]) {
            sa.ip = pres[3][0][0];
        }
        if (connect(SOCKET, &sa, sizeof(SOCK_ADDR)) != -1) {
            CONNECT_STATE = 1;
        } else {
            CONNECT_STATE = 0;
            closesocket(SOCKET);
            GBS_StartTimerProc(&tmr, 216 * 3, Connect_Proc);
        }
    } else {
        CONNECT_STATE = 0;
        GBS_StartTimerProc(&tmr, 216 * 3, Connect_Proc);
    }
}

void Disconnect() {
    if (SOCKET >= 0) {
        CONNECT_STATE = 0;
        shutdown(SOCKET, 2);
        closesocket(SOCKET);
        SOCKET = -1;
    }
}

void Reconnect() {
    Disconnect();
    Connect();
}

void Connect_Proc() {
    SUBPROC(Connect);
}

void Reconnect_Proc() {
    SUBPROC(Reconnect);
}

void SendData() {
    if (!IsPlayerOn()) {
        DATA.status = -1;
    } else {
        DATA.status = GetPlayStatus();
    }
    send(SOCKET, &DATA, sizeof(DATA), 0);
}

void Command() {
     uint8_t cmd;
     if (CONNECT_STATE == 2) {
         if (recv(SOCKET, &cmd, 1, 0)) {
             if (cmd == PLAYER_PREV || cmd == PLAYER_NEXT || cmd == PLAYER_PLAY) {
                 if (!IsPlayerOn()) {
                     MEDIA_PLAYLAST();
                 }
             }
             Send_MPlayer_Command(cmd, 0);
         }
     }
}

int maincsm_onmessage(CSM_RAM *data, GBS_MSG *msg) {
    if (msg->msg == MSG_HELPER_TRANSLATOR) {
        if ((int)msg->data1 == SOCKET) {
            static GBSTMR tmr;
            switch ((int)msg->data0) {
                case ENIP_SOCK_CONNECTED:
                    CONNECT_STATE = 2;
                    SUBPROC(SendData);
                    ShowMSG(1, (int)"Connected!");
                    break;
                case ENIP_SOCK_DATA_READ:
                    SUBPROC(Command);
                    break;
                case ENIP_SOCK_CLOSED: case ENIP_SOCK_REMOTE_CLOSED:
                    if (CONNECT_STATE) {
                        CONNECT_STATE = 0;
                        GBS_StartTimerProc(&tmr, 216 * 3, Reconnect_Proc);
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
    wsprintf((WSHDR *)(&MAINCSM.maincsm_name),"SieMPCtl");
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
