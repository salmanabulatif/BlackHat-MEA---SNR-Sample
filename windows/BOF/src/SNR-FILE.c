//
// WiFi Signal Strength & SNR Monitor BOF
// 
// Collects WiFi signal strength and SNR data over configurable duration
// using WLAN APIs with accurate RSSI from BSS entries
// 
// Compile: x86_64-w64-mingw32-gcc -c SNR-FILE.c -o SNR-FILE.x64.o -mwindows -Os
//

#include <windows.h>
#include <wlanapi.h>
#include "beacon.h"

// memory management macros
#define intAlloc(size) KERNEL32$HeapAlloc(KERNEL32$GetProcessHeap(), HEAP_ZERO_MEMORY, size)
#define intFree(addr) KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, addr)

// configuration constants
#define MAX_SSID_LENGTH 64
#define MAX_SAMPLES 600
#define SAMPLE_INTERVAL_MS 100
#define DEFAULT_DURATION_SEC 5
#define DEFAULT_BASE_DURATION_SEC 3
#define MAX_DURATION_SEC 60

// data structures for signal collection
typedef struct {
    DWORD timestamp_ms;
    int signal_strength_dbm;
    int link_quality;
    int snr_db;
    int noise_floor_dbm;
    char ssid[MAX_SSID_LENGTH];
    ULONG frequency_khz;
    int channel;
} WifiSignalSample;

typedef struct {
    char ssid[MAX_SSID_LENGTH];
    int signal_strength_dbm;
    int link_quality;
    int snr_db;
    int noise_floor_dbm;
    int signal_percent;
    int noise_percent;
    int sample_count;
    ULONG frequency_khz;
    int channel;
} WifiSignalAverage;

// function prototypes
static void escape_json_string(const char* input, char* output, int max_len);
static void append_to_buffer(char** buffer, int* current_pos, int* buffer_size, const char* text);
static void int_to_string(int value, char* buffer, int buffer_size);
static int wifi_calculate_snr(int link_quality, int signal_dbm, int* snr, int* noise);
static int wifi_dbm_to_percent(int dbm_value);
static int wifi_frequency_to_channel(ULONG freq_khz);
static int wifi_get_current_signal(WifiSignalSample* sample);
static int wifi_collect_samples(WifiSignalSample* samples, int* count, int duration_sec);
static int wifi_calculate_average(WifiSignalSample* samples, int count, WifiSignalAverage* avg);
static void send_json_monitor_data(WifiSignalSample* samples, int sample_count);
static void send_json_base_data(WifiSignalAverage* avg);
static void display_signal_samples(WifiSignalSample* samples, int sample_count, int duration_sec);
static void display_base_signal(WifiSignalAverage* avg);

// HELPER FUNCTIONS - String and Buffer Utilities

// escape_json_string - escapes special characters in strings for safe JSON output
// handles quotes, backslashes, newlines, tabs, and carriage returns
// SSIDs containing special characters break JSON parsing
// escape all JSON control characters per RFC 8259
// https://stackoverflow.com/questions/4901133/json-and-escaping-characters
// https://www.rfc-editor.org/rfc/rfc8259#section-7
static void escape_json_string(const char* input, char* output, int max_len) {
    int in_pos = 0;
    int out_pos = 0;

    while (input[in_pos] != '\0' && out_pos < max_len - 2) {
        char c = input[in_pos];

        if (c == '"' || c == '\\') {
            output[out_pos++] = '\\';
            output[out_pos++] = c;
        }
        else if (c == '\n') {
            output[out_pos++] = '\\';
            output[out_pos++] = 'n';
        }
        else if (c == '\r') {
            output[out_pos++] = '\\';
            output[out_pos++] = 'r';
        }
        else if (c == '\t') {
            output[out_pos++] = '\\';
            output[out_pos++] = 't';
        }
        else {
            output[out_pos++] = c;
        }

        in_pos++;
    }

    output[out_pos] = '\0';
}

// append_to_buffer - dynamically grows buffer and appends text
// doubles buffer size when more space is needed
// fixed-size buffers cause overflow with large sample counts poses a problem
// dynamic reallocation with exponential growth 
// https://stackoverflow.com/questions/3536153/c-dynamically-growing-array
// https://stackoverflow.com/questions/1100311/what-is-the-ideal-growth-rate-for-a-dynamically-allocated-array
static void append_to_buffer(char** buffer, int* current_pos, int* buffer_size, const char* text) {
    int text_len = MSVCRT$strlen(text);
    int required_size = *current_pos + text_len + 1;

    // grow buffer if needed
    if (required_size > *buffer_size) {
        int new_size = required_size * 2;
        char* new_buffer = (char*)intAlloc(new_size);

        if (!new_buffer) {
            BeaconPrintf(CALLBACK_ERROR, "[-] Failed to allocate buffer memory\n");
            if (*buffer) {
                intFree(*buffer);
                *buffer = NULL;
            }
            *buffer_size = 0;
            *current_pos = 0;
            return;
        }

        // copy existing data - manual copy to avoid CRT memcpy
        if (*buffer) {
            for (int i = 0; i < *current_pos; i++) {
                new_buffer[i] = (*buffer)[i];
            }
            intFree(*buffer);
        }

        *buffer = new_buffer;
        *buffer_size = new_size;
    }

    // append text
    for (int i = 0; i < text_len; i++) {
        (*buffer)[*current_pos] = text[i];
        (*current_pos)++;
    }

    (*buffer)[*current_pos] = '\0';
}

// int_to_string - converts integer to string manually (no CRT sprintf)
// handles negative numbers and performs in-place string reversal
// Had a problem with sprintf not being available in BOF (no CRT linking)
// Solution was manual conversion with digit extraction and string reversal
// https://stackoverflow.com/questions/8257714/how-to-convert-an-int-to-string-in-c
// https://stackoverflow.com/questions/190229/where-is-the-itoa-function-in-linux
static void int_to_string(int value, char* buffer, int buffer_size) {
    int is_negative = 0;
    int index = 0;
    int temp_value = value;

    if (value < 0) {
        is_negative = 1;
        value = -value;
        temp_value = value;
    }

    // count digits to ensure buffer is large enough
    int digit_count = 0;
    if (temp_value == 0) {
        digit_count = 1;
    }
    else {
        int temp = temp_value;
        while (temp > 0) {
            temp /= 10;
            digit_count++;
        }
    }

    // check buffer size
    if ((is_negative ? digit_count + 1 : digit_count) >= buffer_size) {
        buffer[0] = '\0';
        return;
    }

    // build string in reverse order
    if (value == 0) {
        buffer[index++] = '0';
    }
    else {
        while (value > 0) {
            buffer[index++] = '0' + (value % 10);
            value /= 10;
        }
    }

    if (is_negative) {
        buffer[index++] = '-';
    }

    buffer[index] = '\0';

    // reverse the string in place
    // https://stackoverflow.com/questions/198199/how-do-you-reverse-a-string-in-place-in-c-or-c
    int start = 0;
    int end = index - 1;
    while (start < end) {
        char temp = buffer[start];
        buffer[start] = buffer[end];
        buffer[end] = temp;
        start++;
        end--;
    }
}

// SIGNAL CALCULATIONS - SNR and Conversions

// wifi_calculate_snr - estimates SNR from link quality using empirical ranges
// calculates noise floor as: noise = signal - SNR
// Windows WLAN API doesn't directly expose noise floor which is bad I want accuracy
// Soooo I will derive SNR from link quality percentage using empirical mapping
// https://stackoverflow.com/questions/15797920/how-to-convert-wifi-signal-strength-from-quality-percent-to-rssi-dbm
// https://docs.microsoft.com/en-us/windows/win32/nativewifi/wlan-profileschema-linkquality-wlanprofile-element
static int wifi_calculate_snr(int link_quality, int signal_dbm, int* snr, int* noise) {
    // map link quality ranges to approximate SNR values
    // based on typical WiFi signal quality measurements
    if (link_quality >= 90) {
        *snr = 35 + ((link_quality - 90) / 2);
    } else if (link_quality >= 80) {
        *snr = 30 + ((link_quality - 80) / 2);
    } else if (link_quality >= 70) {
        *snr = 25 + ((link_quality - 70) / 2);
    } else if (link_quality >= 60) {
        *snr = 20 + ((link_quality - 60) / 2);
    } else if (link_quality >= 50) {
        *snr = 15 + ((link_quality - 50) / 2);
    } else if (link_quality >= 40) {
        *snr = 10 + ((link_quality - 40) / 2);
    } else {
        *snr = (link_quality * 10) / 40;
    }
    
    // calculate noise floor using relationship: SNR = Signal - Noise
    *noise = signal_dbm - *snr;
    return 1;
}

// wifi_dbm_to_percent - converts dBm signal strength to percentage (0-100)
// uses standard WiFi signal strength mapping
// dBm values are not intuitive for users to be fair
// so ill convert to percentage using standard WiFi signal quality scale
// https://stackoverflow.com/questions/15797920/how-to-convert-wifi-signal-strength-from-quality-percent-to-rssi-dbm
// formula: percentage = ((dBm + 100) / 70) * 100, clamped to [0, 100]
static int wifi_dbm_to_percent(int dbm_value) {
    if (dbm_value >= -30) return 100;
    if (dbm_value <= -100) return 0;
    
    int percent = ((dbm_value + 100) * 100) / 70;
    
    if (percent < 0) return 0;
    if (percent > 100) return 100;
    
    return percent;
}

// wifi_frequency_to_channel - converts WiFi frequency in kHz to channel number
// supports both 2.4 GHz and 5 GHz bands
// frequency values are not human-readable so thats not good
// convert using standard IEEE 802.11 channel mapping formulas (i still dont know what this means I forgot, re-read)
//  https://en.wikipedia.org/wiki/List_of_WLAN_channels
//  https://stackoverflow.com/questions/11909449/get-wifi-channel-in-c-sharp
// 2.4 GHz: channel = (freq_mhz - 2407) / 5 (channels 1-13)
// 2.4 GHz: channel 14 = 2484 MHz (special case)
// 5 GHz: channel = (freq_mhz - 5000) / 5 (channels 36-165)
static int wifi_frequency_to_channel(ULONG freq_khz) {
    ULONG freq_mhz = freq_khz / 1000;
    
    // 2.4 GHz band (channels 1-14)
    if (freq_mhz >= 2412 && freq_mhz <= 2484) {
        if (freq_mhz == 2484) return 14;
        return (freq_mhz - 2407) / 5;
    }
    
    // 5 GHz band (channels 36-165)
    if (freq_mhz >= 5170 && freq_mhz <= 5825) {
        return (freq_mhz - 5000) / 5;
    }
    
    return 0;
}

// WIFI SIGNAL COLLECTION - Core Functionality

// wifi_get_current_signal - collects a single WiFi signal sample
// uses BSS list for hardware-accurate RSSI measurements
// WlanQueryInterface RSSI values are averaged/smoothed by Windows so thats microsoft
// use WlanGetNetworkBssList to get raw hardware RSSI from BSS entries rather than relying on smoothed values
// https://stackoverflow.com/questions/4228578/get-rssi-value-with-c-sharp
// https://docs.microsoft.com/en-us/windows/win32/api/wlanapi/nf-wlanapi-wlangetnetworkbsslist
// BSS list provides:
// - lRssi: hardware-accurate RSSI in dBm
// - uLinkQuality: 0-100 connection quality
// - ulChCenterFrequency: channel center frequency in kHz
static int wifi_get_current_signal(WifiSignalSample* sample) {
    HANDLE hClient = NULL;
    DWORD dwMaxClient = 2;
    DWORD dwCurVersion = 0;
    
    // open WLAN handle
    // https://docs.microsoft.com/en-us/windows/win32/api/wlanapi/nf-wlanapi-wlanopenhandle
    if (WLANAPI$WlanOpenHandle(dwMaxClient, NULL, &dwCurVersion, &hClient) != ERROR_SUCCESS) {
        return 0;
    }
    
    // enumerate WLAN interfaces
    // https://docs.microsoft.com/en-us/windows/win32/api/wlanapi/nf-wlanapi-wlanenuminterfaces
    PWLAN_INTERFACE_INFO_LIST pIfList = NULL;
    if (WLANAPI$WlanEnumInterfaces(hClient, NULL, &pIfList) != ERROR_SUCCESS) {
        WLANAPI$WlanCloseHandle(hClient, NULL);
        return 0;
    }
    
    if (pIfList->dwNumberOfItems == 0) {
        WLANAPI$WlanFreeMemory(pIfList);
        WLANAPI$WlanCloseHandle(hClient, NULL);
        return 0;
    }
    
    PWLAN_INTERFACE_INFO pIfInfo = &pIfList->InterfaceInfo[0];
    
    // get current connection info
    // https://docs.microsoft.com/en-us/windows/win32/api/wlanapi/nf-wlanapi-wlanqueryinterface
    PWLAN_CONNECTION_ATTRIBUTES pConnectInfo = NULL;
    DWORD connectInfoSize = sizeof(WLAN_CONNECTION_ATTRIBUTES);
    WLAN_OPCODE_VALUE_TYPE opCode;
    
    if (WLANAPI$WlanQueryInterface(hClient, &pIfInfo->InterfaceGuid,
                          wlan_intf_opcode_current_connection,
                          NULL, &connectInfoSize,
                          (PVOID*)&pConnectInfo, &opCode) != ERROR_SUCCESS) {
        WLANAPI$WlanFreeMemory(pIfList);
        WLANAPI$WlanCloseHandle(hClient, NULL);
        return 0;
    }
    
    DOT11_SSID currentSsid = pConnectInfo->wlanAssociationAttributes.dot11Ssid;
    
    // get BSS list for accurate RSSI
    // To not crash again BSS entries are stored in contiguous array, not linked list
    // https://docs.microsoft.com/en-us/windows/win32/api/wlanapi/ns-wlanapi-wlan_bss_list
    // https://stackoverflow.com/questions/32042331/how-to-iterate-wlan-bss-list-correctly
    PWLAN_BSS_LIST pBssList = NULL;
    if (WLANAPI$WlanGetNetworkBssList(hClient, &pIfInfo->InterfaceGuid, NULL,
                              dot11_BSS_type_infrastructure, FALSE,
                              NULL, &pBssList) != ERROR_SUCCESS) {
        WLANAPI$WlanFreeMemory(pConnectInfo);
        WLANAPI$WlanFreeMemory(pIfList);
        WLANAPI$WlanCloseHandle(hClient, NULL);
        return 0;
    }
    
    // find matching BSS entry for our connected network
    // iterate through array (not linked list)
    int found = 0;
    for (DWORD i = 0; i < pBssList->dwNumberOfItems; i++) {
        PWLAN_BSS_ENTRY pBssEntry = &pBssList->wlanBssEntries[i];
        
        if (pBssEntry->dot11Ssid.uSSIDLength == currentSsid.uSSIDLength) {
            int match = 1;
            for (ULONG j = 0; j < currentSsid.uSSIDLength; j++) {
                if (pBssEntry->dot11Ssid.ucSSID[j] != currentSsid.ucSSID[j]) {
                    match = 0;
                    break;
                }
            }
            
            if (match) {
                // extract SSID (no strcpy in BOF)
                ULONG ssidLen = currentSsid.uSSIDLength;
                if (ssidLen > MAX_SSID_LENGTH - 1) ssidLen = MAX_SSID_LENGTH - 1;
                
                for (ULONG k = 0; k < ssidLen; k++) {
                    sample->ssid[k] = (char)currentSsid.ucSSID[k];
                }
                sample->ssid[ssidLen] = '\0';
                
                // get signal measurements from BSS entry
                sample->signal_strength_dbm = pBssEntry->lRssi;
                sample->link_quality = pBssEntry->uLinkQuality;
                sample->frequency_khz = pBssEntry->ulChCenterFrequency;
                sample->channel = wifi_frequency_to_channel(sample->frequency_khz);
                
                wifi_calculate_snr(sample->link_quality, sample->signal_strength_dbm,
                                  &sample->snr_db, &sample->noise_floor_dbm);
                
                sample->timestamp_ms = KERNEL32$GetTickCount();
                found = 1;
                break;
            }
        }
    }
    
    // cleanup - important to avoid memory leaks in beacon process (already happened before LOL)
    // https://docs.microsoft.com/en-us/windows/win32/api/wlanapi/nf-wlanapi-wlanfreememory
    WLANAPI$WlanFreeMemory(pBssList);
    WLANAPI$WlanFreeMemory(pConnectInfo);
    WLANAPI$WlanFreeMemory(pIfList);
    WLANAPI$WlanCloseHandle(hClient, NULL);
    
    return found;
}

// wifi_collect_samples - collects multiple WiFi signal samples over specified duration
// samples are taken at fixed intervals for consistent measurements
// I need consistent sampling rate for time-series analysis
// I guess use ill have fixed interval with GetTickCount for timing
// https://stackoverflow.com/questions/1739259/how-to-use-queryperformancecounter
static int wifi_collect_samples(WifiSignalSample* samples, int* count, int duration_sec) {
    *count = 0;
    
    DWORD start_time = KERNEL32$GetTickCount();
    DWORD duration_ms = duration_sec * 1000;
    int max_samples = (duration_sec * 1000) / SAMPLE_INTERVAL_MS;
    
    if (max_samples > MAX_SAMPLES) max_samples = MAX_SAMPLES;
    
    while (*count < max_samples) {
        DWORD elapsed = KERNEL32$GetTickCount() - start_time;
        if (elapsed >= duration_ms) break;
        
        if (wifi_get_current_signal(&samples[*count])) {
            samples[*count].timestamp_ms = elapsed;
            (*count)++;
        }
        
        KERNEL32$Sleep(SAMPLE_INTERVAL_MS);
    }
    
    return (*count > 0);
}

// wifi_calculate_average - calculates average values from collected signal samples
// used for base mode to get stable signal measurements
// single samples can be noisy due to interference
// average multiple samples for more stable baseline measurement can be an alternative
// https://stackoverflow.com/questions/10930732/c-efficiently-calculating-the-average-of-numbers
static int wifi_calculate_average(WifiSignalSample* samples, int count, WifiSignalAverage* avg) {
    if (count == 0) return 0;
    
    // initialize average structure (no memset in BOF)
    avg->signal_strength_dbm = 0;
    avg->link_quality = 0;
    avg->snr_db = 0;
    avg->noise_floor_dbm = 0;
    avg->signal_percent = 0;
    avg->noise_percent = 0;
    avg->sample_count = 0;
    avg->frequency_khz = 0;
    avg->channel = 0;
    avg->ssid[0] = '\0';
    
    int total_signal = 0;
    int total_quality = 0;
    int total_snr = 0;
    int total_noise = 0;
    
    for (int i = 0; i < count; i++) {
        total_signal += samples[i].signal_strength_dbm;
        total_quality += samples[i].link_quality;
        total_snr += samples[i].snr_db;
        total_noise += samples[i].noise_floor_dbm;
        
        if (i == 0) {
            // copy SSID and frequency info from first sample
            for (int j = 0; j < MAX_SSID_LENGTH; j++) {
                avg->ssid[j] = samples[i].ssid[j];
                if (samples[i].ssid[j] == '\0') break;
            }
            avg->frequency_khz = samples[i].frequency_khz;
            avg->channel = samples[i].channel;
        }
    }
    
    avg->signal_strength_dbm = total_signal / count;
    avg->link_quality = total_quality / count;
    avg->snr_db = total_snr / count;
    avg->noise_floor_dbm = total_noise / count;
    avg->sample_count = count;
    
    avg->signal_percent = wifi_dbm_to_percent(avg->signal_strength_dbm);
    avg->noise_percent = wifi_dbm_to_percent(avg->noise_floor_dbm);
    
    return 1;
}

// JSON OUTPUT - Formatted Data Export

// send_json_monitor_data - exports monitor mode samples to JSON format
// includes dynamic buffer growth and proper JSON escaping
// I need a structured data format for automated parsing
// Have JSON output with markers for easy extraction
// https://stackoverflow.com/questions/4901133/json-and-escaping-characters
// https://www.json.org/json-en.html
static void send_json_monitor_data(WifiSignalSample* samples, int sample_count) {
    char temp[64];
    char line[512];
    char escaped_ssid[MAX_SSID_LENGTH * 2];
    
    // allocate initial buffer with room for growth
    int buffer_size = (sample_count * 250) + 1024;
    char* json_buffer = (char*)intAlloc(buffer_size);
    if (!json_buffer) {
        BeaconPrintf(CALLBACK_ERROR, "[-] Failed to allocate JSON buffer\n");
        return;
    }
    
    int buffer_pos = 0;
    
    // build JSON structure with markers for extraction
    append_to_buffer(&json_buffer, &buffer_pos, &buffer_size, "\n[JSON_START]\n");
    append_to_buffer(&json_buffer, &buffer_pos, &buffer_size, "{\n");
    append_to_buffer(&json_buffer, &buffer_pos, &buffer_size, "  \"collection_type\": \"monitor\",\n");
    append_to_buffer(&json_buffer, &buffer_pos, &buffer_size, "  \"samples\": [\n");
    
    for (int i = 0; i < sample_count; i++) {
        WifiSignalSample* s = &samples[i];
        
        escape_json_string(s->ssid, escaped_ssid, sizeof(escaped_ssid));
        
        append_to_buffer(&json_buffer, &buffer_pos, &buffer_size, "    {\n");
        
        // add each field to JSON using manual int conversion
        int_to_string(s->timestamp_ms, temp, sizeof(temp));
        MSVCRT$sprintf(line, "      \"timestamp_ms\": %s,\n", temp);
        append_to_buffer(&json_buffer, &buffer_pos, &buffer_size, line);
        
        int_to_string(s->signal_strength_dbm, temp, sizeof(temp));
        MSVCRT$sprintf(line, "      \"signal_strength_dbm\": %s,\n", temp);
        append_to_buffer(&json_buffer, &buffer_pos, &buffer_size, line);
        
        int_to_string(s->link_quality, temp, sizeof(temp));
        MSVCRT$sprintf(line, "      \"link_quality\": %s,\n", temp);
        append_to_buffer(&json_buffer, &buffer_pos, &buffer_size, line);
        
        int_to_string(s->snr_db, temp, sizeof(temp));
        MSVCRT$sprintf(line, "      \"snr_db\": %s,\n", temp);
        append_to_buffer(&json_buffer, &buffer_pos, &buffer_size, line);
        
        int_to_string(s->noise_floor_dbm, temp, sizeof(temp));
        MSVCRT$sprintf(line, "      \"noise_floor_dbm\": %s,\n", temp);
        append_to_buffer(&json_buffer, &buffer_pos, &buffer_size, line);
        
        MSVCRT$sprintf(line, "      \"ssid\": \"%s\",\n", escaped_ssid);
        append_to_buffer(&json_buffer, &buffer_pos, &buffer_size, line);
        
        int_to_string(s->frequency_khz, temp, sizeof(temp));
        MSVCRT$sprintf(line, "      \"frequency_khz\": %s,\n", temp);
        append_to_buffer(&json_buffer, &buffer_pos, &buffer_size, line);
        
        int_to_string(s->channel, temp, sizeof(temp));
        MSVCRT$sprintf(line, "      \"channel\": %s\n", temp);
        append_to_buffer(&json_buffer, &buffer_pos, &buffer_size, line);
        
        if (i < sample_count - 1) {
            append_to_buffer(&json_buffer, &buffer_pos, &buffer_size, "    },\n");
        } else {
            append_to_buffer(&json_buffer, &buffer_pos, &buffer_size, "    }\n");
        }
    }
    
    append_to_buffer(&json_buffer, &buffer_pos, &buffer_size, "  ],\n");
    
    int_to_string(sample_count, temp, sizeof(temp));
    MSVCRT$sprintf(line, "  \"total_samples\": %s\n}\n", temp);
    append_to_buffer(&json_buffer, &buffer_pos, &buffer_size, line);
    
    append_to_buffer(&json_buffer, &buffer_pos, &buffer_size, "[JSON_END]\n\n");
    
    // send complete JSON to beacon
    BeaconPrintf(CALLBACK_OUTPUT, "%s", json_buffer);
    BeaconPrintf(CALLBACK_OUTPUT, "[+] JSON data sent via beacon callback (%d bytes)\n", buffer_pos);
    BeaconPrintf(CALLBACK_OUTPUT, "[*] Copy JSON between [JSON_START] and [JSON_END] markers\n");
    
    intFree(json_buffer);
}

// send_json_base_data - exports base mode averaged data to JSON format
static void send_json_base_data(WifiSignalAverage* avg) {
    char temp[64];
    char line[512];
    char escaped_ssid[MAX_SSID_LENGTH * 2];
    
    int buffer_size = 2048;
    char* json_buffer = (char*)intAlloc(buffer_size);
    if (!json_buffer) {
        BeaconPrintf(CALLBACK_ERROR, "[-] Failed to allocate JSON buffer\n");
        return;
    }
    
    int buffer_pos = 0;
    
    escape_json_string(avg->ssid, escaped_ssid, sizeof(escaped_ssid));
    
    append_to_buffer(&json_buffer, &buffer_pos, &buffer_size, "\n[JSON_START]\n");
    append_to_buffer(&json_buffer, &buffer_pos, &buffer_size, "{\n");
    append_to_buffer(&json_buffer, &buffer_pos, &buffer_size, "  \"collection_type\": \"base\",\n");
    
    MSVCRT$sprintf(line, "  \"ssid\": \"%s\",\n", escaped_ssid);
    append_to_buffer(&json_buffer, &buffer_pos, &buffer_size, line);
    
    int_to_string(avg->sample_count, temp, sizeof(temp));
    MSVCRT$sprintf(line, "  \"sample_count\": %s,\n", temp);
    append_to_buffer(&json_buffer, &buffer_pos, &buffer_size, line);
    
    int_to_string(avg->frequency_khz, temp, sizeof(temp));
    MSVCRT$sprintf(line, "  \"frequency_khz\": %s,\n", temp);
    append_to_buffer(&json_buffer, &buffer_pos, &buffer_size, line);
    
    int_to_string(avg->channel, temp, sizeof(temp));
    MSVCRT$sprintf(line, "  \"channel\": %s,\n", temp);
    append_to_buffer(&json_buffer, &buffer_pos, &buffer_size, line);
    
    int_to_string(avg->signal_strength_dbm, temp, sizeof(temp));
    MSVCRT$sprintf(line, "  \"signal_strength_dbm\": %s,\n", temp);
    append_to_buffer(&json_buffer, &buffer_pos, &buffer_size, line);
    
    int_to_string(avg->link_quality, temp, sizeof(temp));
    MSVCRT$sprintf(line, "  \"link_quality\": %s,\n", temp);
    append_to_buffer(&json_buffer, &buffer_pos, &buffer_size, line);
    
    int_to_string(avg->snr_db, temp, sizeof(temp));
    MSVCRT$sprintf(line, "  \"snr_db\": %s,\n", temp);
    append_to_buffer(&json_buffer, &buffer_pos, &buffer_size, line);
    
    int_to_string(avg->noise_floor_dbm, temp, sizeof(temp));
    MSVCRT$sprintf(line, "  \"noise_floor_dbm\": %s,\n", temp);
    append_to_buffer(&json_buffer, &buffer_pos, &buffer_size, line);
    
    int_to_string(avg->signal_percent, temp, sizeof(temp));
    MSVCRT$sprintf(line, "  \"signal_percent\": %s,\n", temp);
    append_to_buffer(&json_buffer, &buffer_pos, &buffer_size, line);
    
    int_to_string(avg->noise_percent, temp, sizeof(temp));
    MSVCRT$sprintf(line, "  \"noise_percent\": %s\n}\n", temp);
    append_to_buffer(&json_buffer, &buffer_pos, &buffer_size, line);
    
    append_to_buffer(&json_buffer, &buffer_pos, &buffer_size, "[JSON_END]\n\n");
    
    BeaconPrintf(CALLBACK_OUTPUT, "%s", json_buffer);
    BeaconPrintf(CALLBACK_OUTPUT, "[+] JSON data sent via beacon callback (%d bytes)\n", buffer_pos);
    BeaconPrintf(CALLBACK_OUTPUT, "[*] Copy JSON between [JSON_START] and [JSON_END] markers\n");
    
    intFree(json_buffer);
}

// DISPLAY FUNCTIONS - Human-Readable Output

// display_signal_samples - displays collected signal samples in table format
// consolidates all output into a single buffer to prevent fragmented beacon callbacks
// allocates buffer on heap to avoid stack overflow with large sample counts
static void display_signal_samples(WifiSignalSample* samples, int sample_count, int duration_sec) {
    if (sample_count == 0) {
        BeaconPrintf(CALLBACK_OUTPUT, "[-] No samples to display\n");
        return;
    }
    
    // calculate buffer size needed
    // header: ~200 bytes, each sample line: ~80 bytes, footer: ~100 bytes
    int buffer_size = 512 + (sample_count * 100);
    char* output = (char*)intAlloc(buffer_size);
    if (!output) {
        BeaconPrintf(CALLBACK_ERROR, "[-] Failed to allocate display buffer\n");
        return;
    }
    
    int pos = 0;
    
    // build header
    pos += MSVCRT$sprintf(&output[pos], "=== Raw WiFi Signal Data (BSS Accurate RSSI) ===\n");
    pos += MSVCRT$sprintf(&output[pos], "SSID: %s\n", samples[0].ssid);
    pos += MSVCRT$sprintf(&output[pos], "Duration: %d seconds\n", duration_sec);
    
    if (samples[0].frequency_khz > 0) {
        pos += MSVCRT$sprintf(&output[pos], "Frequency: %lu kHz (Channel %d)\n\n",
            samples[0].frequency_khz, samples[0].channel);
    } else {
        pos += MSVCRT$sprintf(&output[pos], "\n");
    }
    
    // build table header
    pos += MSVCRT$sprintf(&output[pos], "Time(ms) | RSSI(dBm) | Quality(%%) | SNR(dB) | Noise(dBm)\n");
    pos += MSVCRT$sprintf(&output[pos], "---------+------------+------------+---------+-----------\n");
    
    // build all sample rows
    for (int i = 0; i < sample_count; i++) {
        // check if we need more space (with safety margin)
        if (pos + 100 >= buffer_size) {
            // grow buffer
            int new_size = buffer_size * 2;
            char* new_output = (char*)intAlloc(new_size);
            if (!new_output) {
                BeaconPrintf(CALLBACK_ERROR, "[-] Failed to grow display buffer\n");
                intFree(output);
                return;
            }
            
            // copy existing data
            for (int j = 0; j < pos; j++) {
                new_output[j] = output[j];
            }
            
            intFree(output);
            output = new_output;
            buffer_size = new_size;
        }
        
        pos += MSVCRT$sprintf(&output[pos], "%8d | %10d | %10d | %7d | %10d\n",
            samples[i].timestamp_ms,
            samples[i].signal_strength_dbm,
            samples[i].link_quality,
            samples[i].snr_db,
            samples[i].noise_floor_dbm);
    }
    
    // build footer
    pos += MSVCRT$sprintf(&output[pos], "\nTotal samples: %d\n", sample_count);
    pos += MSVCRT$sprintf(&output[pos], "Note: Using hardware-accurate RSSI from BSS entries\n");
    
    // send complete output in one call
    BeaconPrintf(CALLBACK_OUTPUT, "%s", output);
    
    intFree(output);
}

// display_base_signal - displays averaged base signal data with quality assessment
// need to avoid stack overflow with large output strings
// allocate display buffer on heap instead of stack
// https://stackoverflow.com/questions/1847789/stack-vs-heap-allocation-of-structs-in-c-and-performance
// https://stackoverflow.com/questions/79923/what-and-where-are-the-stack-and-heap
static void display_base_signal(WifiSignalAverage* avg) {
    char* output = (char*)intAlloc(4096);
    if (!output) {
        BeaconPrintf(CALLBACK_ERROR, "[-] Failed to allocate display buffer\n");
        return;
    }
    
    int pos = 0;
    
    pos += MSVCRT$sprintf(&output[pos], "\n=== Base WiFi Signal Capture (BSS Accurate RSSI) ===\n");
    pos += MSVCRT$sprintf(&output[pos], "SSID: %s\n", avg->ssid);
    pos += MSVCRT$sprintf(&output[pos], "Samples Averaged: %d\n", avg->sample_count);
    
    if (avg->frequency_khz > 0) {
        pos += MSVCRT$sprintf(&output[pos], "Frequency: %lu kHz (Channel %d)\n\n",
            avg->frequency_khz, avg->channel);
    } else {
        pos += MSVCRT$sprintf(&output[pos], "\n");
    }
    
    pos += MSVCRT$sprintf(&output[pos], "Averaged Signal Measurements (Accurate BSS RSSI):\n");
    pos += MSVCRT$sprintf(&output[pos], "  Signal Strength (RSSI): %d dBm (%d%%)\n",
        avg->signal_strength_dbm, avg->signal_percent);
    pos += MSVCRT$sprintf(&output[pos], "  Link Quality: %d%%\n", avg->link_quality);
    pos += MSVCRT$sprintf(&output[pos], "  SNR: %d dB\n", avg->snr_db);
    pos += MSVCRT$sprintf(&output[pos], "  Noise Floor: %d dBm (%d%%)\n",
        avg->noise_floor_dbm, avg->noise_percent);
    
    pos += MSVCRT$sprintf(&output[pos], "\nSignal Quality:\n");
    if (avg->signal_percent >= 85) {
        pos += MSVCRT$sprintf(&output[pos], "  [+] Excellent (%d%%) - Very close\n", avg->signal_percent);
    }
    else if (avg->signal_percent >= 70) {
        pos += MSVCRT$sprintf(&output[pos], "  [+] Good (%d%%) - Close proximity\n", avg->signal_percent);
    }
    else if (avg->signal_percent >= 50) {
        pos += MSVCRT$sprintf(&output[pos], "  [~] Fair (%d%%) - Medium distance\n", avg->signal_percent);
    }
    else if (avg->signal_percent >= 30) {
        pos += MSVCRT$sprintf(&output[pos], "  [-] Poor (%d%%) - Far distance\n", avg->signal_percent);
    }
    else {
        pos += MSVCRT$sprintf(&output[pos], "  [!] Very Poor (%d%%) - Very far\n", avg->signal_percent);
    }
    
    BeaconPrintf(CALLBACK_OUTPUT, "%s", output);
    intFree(output);
}

// BOF ENTRY POINT - Main Execution Function
// supports both monitor mode (continuous sampling) and base mode (averaged)
// need flexible argument parsing 
// manual string parsing with ASCII digit conversion
// https://stackoverflow.com/questions/7021725/how-to-convert-a-string-to-integer-in-c
// usage:
//   inline-execute SNR-FILE.x64.o base 5      (base mode, 5 seconds)
//   inline-execute SNR-FILE.x64.o monitor 10  (monitor mode, 10 seconds)
VOID go(IN PCHAR Buffer, IN ULONG Length) {
    int duration_sec = DEFAULT_DURATION_SEC;
    int use_base_mode = 0;
    int base_duration = DEFAULT_BASE_DURATION_SEC;
    
    // parse arguments if provided
    if (Length > 0 && Buffer != NULL) {
        char arg_copy[256] = { 0 };
        
        // copy arguments to safe buffer to prevent overrun
        int copy_len = (Length < 255) ? Length : 255;
        for (int i = 0; i < copy_len; i++) {
            arg_copy[i] = Buffer[i];
        }
        arg_copy[copy_len] = '\0';
        
        // extract mode string
        char mode[32] = { 0 };
        int mode_len = 0;
        
        while (arg_copy[mode_len] && arg_copy[mode_len] != ' ' && mode_len < 31) {
            mode[mode_len] = arg_copy[mode_len];
            mode_len++;
        }
        mode[mode_len] = '\0';
        
        // check for base mode (case-insensitive comparison)
        if ((mode[0] == 'b' || mode[0] == 'B') &&
            (mode[1] == 'a' || mode[1] == 'A') &&
            (mode[2] == 's' || mode[2] == 'S') &&
            (mode[3] == 'e' || mode[3] == 'E')) {
            
            use_base_mode = 1;
            
            // parse duration if provided (manual atoi implementation)
            char* duration_str = arg_copy + mode_len;
            while (*duration_str == ' ') duration_str++;
            
            if (*duration_str >= '0' && *duration_str <= '9') {
                base_duration = 0;
                while (*duration_str >= '0' && *duration_str <= '9') {
                    base_duration = base_duration * 10 + (*duration_str - '0');
                    duration_str++;
                }
                
                if (base_duration < 1 || base_duration > MAX_DURATION_SEC) {
                    BeaconPrintf(CALLBACK_OUTPUT, "[*] Invalid duration, using default: %d seconds\n",
                        DEFAULT_BASE_DURATION_SEC);
                    base_duration = DEFAULT_BASE_DURATION_SEC;
                }
            }
        }
        // check for monitor mode
        else if ((mode[0] == 'm' || mode[0] == 'M') &&
            (mode[1] == 'o' || mode[1] == 'O') &&
            (mode[2] == 'n' || mode[2] == 'N') &&
            (mode[3] == 'i' || mode[3] == 'I')) {
            
            use_base_mode = 0;
            
            char* duration_str = arg_copy + mode_len;
            while (*duration_str == ' ') duration_str++;
            
            if (*duration_str >= '0' && *duration_str <= '9') {
                duration_sec = 0;
                while (*duration_str >= '0' && *duration_str <= '9') {
                    duration_sec = duration_sec * 10 + (*duration_str - '0');
                    duration_str++;
                }
                
                if (duration_sec < 1 || duration_sec > MAX_DURATION_SEC) {
                    BeaconPrintf(CALLBACK_OUTPUT, "[*] Invalid duration, using default: %d seconds\n",
                        DEFAULT_DURATION_SEC);
                    duration_sec = DEFAULT_DURATION_SEC;
                }
            }
        }
        else {
            BeaconPrintf(CALLBACK_OUTPUT, "[*] No valid mode specified, using default: monitor mode\n");
        }
    }
    
    BeaconPrintf(CALLBACK_OUTPUT, "\n=== WiFi Signal Strength & SNR Monitor ===\n");
    BeaconPrintf(CALLBACK_OUTPUT, "Educational Purpose - Accurate BSS RSSI Collection\n\n");
    
    // execute base mode - averaged signal measurements
    if (use_base_mode) {
        BeaconPrintf(CALLBACK_OUTPUT, "[*] Mode: Base (BSS accurate RSSI over %d seconds)\n\n",
            base_duration);
        
        // allocate sample buffer for averaging
        int max_samples = (base_duration * 1000) / SAMPLE_INTERVAL_MS;
        WifiSignalSample* samples = (WifiSignalSample*)intAlloc(sizeof(WifiSignalSample) * max_samples);
        if (!samples) {
            BeaconPrintf(CALLBACK_ERROR, "[-] Failed to allocate sample buffer\n");
            return;
        }
        
        int sample_count = 0;
        
        if (wifi_collect_samples(samples, &sample_count, base_duration) && sample_count > 0) {
            WifiSignalAverage avg;
            if (wifi_calculate_average(samples, sample_count, &avg)) {
                display_base_signal(&avg);
                send_json_base_data(&avg);
            }
        }
        else {
            BeaconPrintf(CALLBACK_ERROR, "[-] Failed to capture base signal data\n");
        }
        
        intFree(samples);
        BeaconPrintf(CALLBACK_OUTPUT, "\n[*] Base capture complete\n");
        return;
    }
    
    // execute monitor mode - continuous sampling
    int sample_count_target = (duration_sec * 1000) / SAMPLE_INTERVAL_MS;
    
    WifiSignalSample* samples = (WifiSignalSample*)intAlloc(sizeof(WifiSignalSample) * sample_count_target);
    if (!samples) {
        BeaconPrintf(CALLBACK_ERROR, "[-] Failed to allocate sample buffer\n");
        return;
    }
    
    int sample_count = 0;
    
    BeaconPrintf(CALLBACK_OUTPUT, "[*] Starting %d-second WiFi signal collection (BSS RSSI)...\n", duration_sec);
    BeaconPrintf(CALLBACK_OUTPUT, "[*] Sample interval: %d ms\n", SAMPLE_INTERVAL_MS);
    BeaconPrintf(CALLBACK_OUTPUT, "[*] Target samples: %d\n\n", sample_count_target);
    
    DWORD start_time = KERNEL32$GetTickCount();
    
    if (wifi_collect_samples(samples, &sample_count, duration_sec)) {
        DWORD end_time = KERNEL32$GetTickCount();
        DWORD elapsed = end_time - start_time;
        
        BeaconPrintf(CALLBACK_OUTPUT, "\n[+] Collection complete\n");
        BeaconPrintf(CALLBACK_OUTPUT, "[+] Collected %d samples in %d ms\n", sample_count, elapsed);
        BeaconPrintf(CALLBACK_OUTPUT, "[+] Actual sample rate: ~%d ms\n\n",
            sample_count > 0 ? elapsed / sample_count : 0);
        
        display_signal_samples(samples, sample_count, duration_sec);
        send_json_monitor_data(samples, sample_count);
    }
    else {
        BeaconPrintf(CALLBACK_ERROR, "[-] Failed to collect WiFi signal strength data\n");
        BeaconPrintf(CALLBACK_OUTPUT, "[*] Note: WiFi adapter may not be available or not connected\n");
    }
    
    intFree(samples);
    BeaconPrintf(CALLBACK_OUTPUT, "\n[*] Monitoring complete\n");
}