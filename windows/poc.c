#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <time.h>
#include <ctype.h>

#define MAX_BUFFER 8192
#define MAX_SSID_LENGTH 64
#define REFRESH_INTERVAL_MS 1100

void safe_strcpy(char *dest, size_t dest_size, const char *src) {
    if (dest && src && dest_size > 0) {
        strcpy_s(dest, dest_size, src);
    }
}

int run_netsh(char *output, size_t output_size) {
    FILE *fp = _popen("netsh wlan show interfaces", "r");
    if (!fp) return 0;

    output[0] = '\0';
    char line[512];
    size_t total = 0;

    while (fgets(line, sizeof(line), fp)) {
        if (total + strlen(line) >= output_size - 1) break;
        strcat_s(output, output_size, line);
        total += strlen(line);
    }

    _pclose(fp);
    return 1;
}

int extract_value(const char *output, const char *field, char *result, size_t result_size) {
    if (!output || !field || !result || result_size == 0) return 0;

    const char *p = strstr(output, field);
    if (!p) return 0;

    p = strchr(p, ':');
    if (!p) return 0;
    p++;

    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0' || *p == '\r' || *p == '\n') return 0;

    const char *end = p;
    while (*end && *end != '\r' && *end != '\n') end++;

    size_t len = end - p;
    if (len >= result_size) len = result_size - 1;

    strncpy_s(result, result_size, p, len);
    result[len] = '\0';

    char *e = result + len - 1;
    while (e >= result && (*e == ' ' || *e == '\t' || *e == '\r' || *e == '\n')) {
        *e = '\0';
        e--;
    }

    return 1;
}

int has_wifi_capability() {
    FILE *fp = _popen("netsh wlan show drivers", "r");
    if (!fp) return 0;

    char buffer[512];
    int found = 0;
    while (fgets(buffer, sizeof(buffer), fp)) {
        if (strstr(buffer, "Radio types supported") || strstr(buffer, "802.11")) {
            found = 1;
            break;
        }
    }
    _pclose(fp);
    return found;
}

int main(void) {
    SetConsoleOutputCP(CP_UTF8);
    system("chcp 65001 >nul");
    system("cls");

    printf("=== Wi-Fi Proximity Monitor - Live SNR Feed ===\n");
    printf("Black Hat MEA 2025 - Educational Use Only\n");
    printf("================================================\n\n");

    if (!has_wifi_capability()) {
        printf("ERROR: No Wi-Fi adapter detected or Wi-Fi is disabled.\n");
        printf("Please enable your Wi-Fi adapter and try again.\n");
        system("pause");
        return 1;
    }

    printf("Time     | SSID                  | Signal       | Est. SNR | Status\n");
    printf("---------------------------------------------------------------------\n");

    char output[MAX_BUFFER] = {0};
    char ssid[MAX_SSID_LENGTH] = {0};
    char signal_str[16] = {0};
    char state_str[32] = {0};
    int was_connected = 0;
    int errors = 0;

    while (1) {
        if (!run_netsh(output, sizeof(output))) {
            errors++;
            printf("\rQuery failed (%d)...", errors);
            fflush(stdout);
            if (errors > 10) {
                printf("\nToo many errors. Exiting.\n");
                return 1;
            }
            Sleep(REFRESH_INTERVAL_MS);
            continue;
        }
        errors = 0;

        time_t now = time(NULL);
        struct tm tm_info;
        localtime_s(&tm_info, &now);
        char time_str[10];
        strftime(time_str, sizeof(time_str), "%H:%M:%S", &tm_info);

        int is_connected = 0;
        state_str[0] = '\0';
        if (extract_value(output, "State", state_str, sizeof(state_str))) {
            for (char *c = state_str; *c; c++) *c = tolower(*c);
            if (strstr(state_str, "connected")) is_connected = 1;
        }

        if (is_connected != was_connected) {
            if (is_connected) {
                printf("\n[+] Connected! Starting live monitoring...\n\n");
            } else {
                printf("\n[-] Disconnected. Waiting for Wi-Fi connection...\n\n");
            }
            was_connected = is_connected;
        }

        if (!is_connected) {
            printf("\r%-8s | %-20s | %-12s | %-8s | Not connected", time_str, "", "", "");
            fflush(stdout);
            Sleep(REFRESH_INTERVAL_MS);
            continue;
        }

        ssid[0] = '\0';
        if (!extract_value(output, "SSID", ssid, sizeof(ssid)) || strlen(ssid) == 0) {
            safe_strcpy(ssid, sizeof(ssid), "Hidden/Unknown");
        }

        signal_str[0] = '\0';
        if (!extract_value(output, "Signal", signal_str, sizeof(signal_str))) {
            strcpy_s(signal_str, sizeof(signal_str), "0%");
        }

        int signal_pct = 0;
        if (sscanf_s(signal_str, "%d%%", &signal_pct) != 1) {
            signal_pct = 0;
        }

        float signal_dbm = (signal_pct / 2.0f) - 100.0f;
        if (signal_pct >= 100) signal_dbm = -30.0f;
        if (signal_pct <= 0) signal_dbm = -100.0f;

        float snr = signal_dbm - (-95.0f);  

        const char *status;
        if (snr >= 40) status = "AI";
        else if (snr >= 38) status = "AI";
        else if (snr >= 35) status = "AI";
        else if (snr >= 32) status = "AI";
        else status = "AI";

        char display_ssid[21] = {0};
        strncpy_s(display_ssid, sizeof(display_ssid), ssid, 20);
        if (strlen(ssid) > 20) {
            strcpy_s(display_ssid + 17, 4, "...");
        }

        printf("\r%-8s | %-20s | %3d%% (%+5.1f dBm) | %5.1f dB | %s     ",
               time_str, display_ssid, signal_pct, signal_dbm, snr, status);
        fflush(stdout);

        Sleep(REFRESH_INTERVAL_MS);
    }

    return 0;
}
