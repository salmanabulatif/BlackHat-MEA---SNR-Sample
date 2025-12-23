# Wi-Fi SNR Monitor - Beacon Object File (BOF)

> ⚠️ **RED TEAM TOOL - AUTHORIZED USE ONLY**  
> This BOF is designed for authorized penetration testing and red team operations.  
> Requires explicit written permission before deployment.

---

## Overview

This Beacon Object File (BOF) enables real-time Wi-Fi signal monitoring directly from a Cobalt Strike beacon. It collects Signal-to-Noise Ratio (SNR) data to detect physical proximity changes without requiring external tools or spawning suspicious processes.


## Compilation

### Using MinGW-w64 (Recommended)

**For x64 architecture:**
```bash
x86_64-w64-mingw32-gcc -c SNR-FILE.c -o SNR-FILE.x64.o -mwindows -Os
```



## Loading into Cobalt Strike

### Manual Loading
```
beacon> inline-execute /path/to/SNR-FILE.x64.o
```


## Usage

```
beacon> inline-execute /home/kali/SNR-FILE.o
```
Collects Wi-Fi signal data for the default duration (5 seconds).

### Custom Duration
```
beacon> inline-execute /home/kali/SNR-FILE.o monitor 10
```




## Security & Ethics

### Authorized Use Only
This tool must only be used in:
- Authorized penetration tests with signed agreements
- Red team exercises with proper authorization
- Security research with institutional approval
- Controlled lab environments

### Legal Requirements
- Obtain explicit written permission before deployment
- Document all usage and findings
- Comply with local wireless communication laws
- Respect privacy and data protection regulations

### Prohibited Actions
❌ Unauthorized network monitoring  
❌ Stalking or surveillance  
❌ Corporate espionage  
❌ Any illegal activities  

---

## 🔗 References

### Windows WLAN API Documentation
- [WlanOpenHandle](https://docs.microsoft.com/en-us/windows/win32/api/wlanapi/nf-wlanapi-wlanopenhandle)
- [WlanEnumInterfaces](https://docs.microsoft.com/en-us/windows/win32/api/wlanapi/nf-wlanapi-wlanenuminterfaces)
- [WlanQueryInterface](https://docs.microsoft.com/en-us/windows/win32/api/wlanapi/nf-wlanapi-wlanqueryinterface)
- [WlanGetNetworkBssList](https://docs.microsoft.com/en-us/windows/win32/api/wlanapi/nf-wlanapi-wlangetnetworkbsslist)

### Cobalt Strike BOF Development
- [BOF Development Guide](https://hstechdocs.helpsystems.com/manuals/cobaltstrike/current/userguide/content/topics/beacon-object-files_main.htm)
- [Beacon API Reference](https://hstechdocs.helpsystems.com/manuals/cobaltstrike/current/userguide/content/topics/beacon-object-files_beacon-api.htm)

