/* SPDX-License-Identifier: GPL-2.0-only
 * Data-only virtual modem for AOSP reference-ril (Android 4–6).
 * Registration describes the virtual NAT link, not host Internet reachability.
 * No calls/SMS, physical SIM access, or host cellular control.
 */
#ifndef ANDROIDEMU_GSM_H
#define ANDROIDEMU_GSM_H
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
typedef struct GFGSM {
    bool radio, context, active;
    unsigned creg, cgreg;
    char apn[128];
} GFGSM;

static void gf_gsm_command(GFGSM *s, const char *cmd, char out[1024])
{
    const char *reply = NULL;
    char value[768] = "";
    static const struct { const char *cmd, *answer; } sim[] = {
#include "gsm_sim.inc"
    };
    static const char *setup[] = {
        "AT", "ATE0Q0V1", "ATS0=0", "AT+CMEE=1", "AT+CCWA=1",
        "AT+CMOD=0", "AT+CMUT=0", "AT+CSSN=0,1", "AT+COLP=0",
        "AT+CSCS=\"HEX\"", "AT+CUSD=1", "AT+CUSD=2", "AT+CGEREP=1,0",
        "AT+CMGF=0", "AT%CPI=3", "AT%CSTAT=1", "AT%CPHS=1", "AT%CTZV=1",
        "AT+CNMI=1,2,2,1,1", "AT+COPS=0", "AT+CGQREQ=1", "AT+CGQMIN=1",
        "AT+CLCC",
    };
    for (unsigned i = 0; i < sizeof(setup) / sizeof(setup[0]); ++i) {
        if (!strcmp(cmd, setup[i])) { reply = ""; break; }
    }
    if (!strcmp(cmd, "AT+CFUN?")) {
        snprintf(value, sizeof(value), "+CFUN: %d", s->radio); reply = value;
    } else if (!strcmp(cmd, "AT+CFUN=0") || !strcmp(cmd, "AT+CFUN=1")) {
        s->radio = cmd[8] == '1';
        if (!s->radio) { s->active = false; }
        snprintf(out, 1024, "\r\nOK\r\n+CREG: %d\r\n+CGREG: %d\r\n", s->radio, s->radio);
        return;
    } else if (!strcmp(cmd, "AT+CPIN?")) { reply = s->radio ? "+CPIN: READY" : "+CME ERROR: 10";
    } else if (!strcmp(cmd, "AT+CIMI")) { reply = "310260000000000";
    } else if (!strcmp(cmd, "AT+CGSN")) { reply = "000000000000000";
    } else if (!strcmp(cmd, "AT+CGMR")) { reply = "AndroidEmu data modem";
    } else if (!strcmp(cmd, "AT+CSQ")) { reply = s->radio ? "+CSQ: 23,0" : "+CSQ: 99,99";
    } else if (!strcmp(cmd, "AT+CSMS=1")) { reply = "+CSMS: 1,1,1";
    } else if (!strcmp(cmd, "AT+COPS?")) { reply = s->radio ? "+COPS: 0,2,\"310260\"" : "+COPS: 0";
    } else if (!strcmp(cmd, "AT+COPS=3,0;+COPS?;+COPS=3,1;+COPS?;+COPS=3,2;+COPS?")) {
        reply = s->radio ? "+COPS: 0,0,\"Android\"\r\n+COPS: 0,1,\"Android\"\r\n+COPS: 0,2,\"310260\"" :
                          "+COPS: 0\r\n+COPS: 0\r\n+COPS: 0";
    } else if (!strcmp(cmd, "AT+CREG?") || !strcmp(cmd, "AT+CGREG?")) {
        bool data = cmd[4] == 'G';
        unsigned mode = data ? s->cgreg : s->creg;
        snprintf(value, sizeof(value), "+%s: %u,%d%s", data ? "CGREG" : "CREG", mode, s->radio,
                 data ? ",\"0001\",\"0001\",3" : ",\"0001\",\"0001\"");
        reply = value;
    } else if ((!strncmp(cmd, "AT+CREG=", 8) && strlen(cmd) == 9 && cmd[8] >= '0' && cmd[8] <= '2') ||
               (!strncmp(cmd, "AT+CGREG=", 9) && strlen(cmd) == 10 && cmd[9] >= '0' && cmd[9] <= '2')) {
        if (cmd[4] == 'G') { s->cgreg = cmd[9] - '0'; } else { s->creg = cmd[8] - '0'; }
        reply = "";
    } else if (!strncmp(cmd, "AT+CRSM=", 8)) {
        reply = "+CRSM: 148,4"; // Unknown SIM file, not successful fabricated data.
        for (unsigned i = 0; i < sizeof(sim) / sizeof(sim[0]); ++i) {
            if (!strcmp(cmd + 2, sim[i].cmd)) { reply = sim[i].answer; break; }
        }
    } else if (!strncmp(cmd, "AT+CGDCONT=1,\"IP\",\"", 19) ||
               !strncmp(cmd, "AT+CGDCONT=1,\"IPV4V6\",\"", 23)) {
        // A dual-stack APN may negotiate IPv4 on this IPv4-only NAT.
        const char *apn = cmd + (!strncmp(cmd, "AT+CGDCONT=1,\"IP\",\"", 19) ? 19 : 23);
        const char *end = strchr(apn, '"');
        if (end && (size_t)(end - apn) < sizeof(s->apn)) {
            size_t n = end - apn;
            memcpy(s->apn, apn, n); s->apn[n] = 0;
            s->context = true; reply = "";
        }
    } else if (!strcmp(cmd, "AT+CGDCONT?")) {
        if (s->context) { snprintf(value, sizeof(value), "+CGDCONT: 1,\"IP\",\"%s\",\"10.0.2.15\",0,0", s->apn); }
        reply = value;
    } else if (!strcmp(cmd, "AT+CGACT?")) {
        if (s->context) { snprintf(value, sizeof(value), "+CGACT: 1,%d", s->active); }
        reply = value;
    } else if (!strcmp(cmd, "AT+CGACT=1,0") || !strcmp(cmd, "AT+CGACT=0,1")) {
        s->active = false; reply = "";
    } else if (!strcmp(cmd, "ATD*99***1#") && s->radio && s->context) {
        s->active = true; reply = "";
    }
    if (reply && strncmp(reply, "+CME ERROR:", 11)) {
        snprintf(out, 1024, "\r\n%s%sOK\r\n", reply, *reply ? "\r\n" : "");
    } else { snprintf(out, 1024, "\r\n%s\r\n", reply ? reply : "ERROR"); }
}
#endif
