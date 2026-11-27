#pragma once

#include "bass.h"

namespace svplus::client::audio {

struct BassApi {
    DWORD(WINAPI* GetVersion)(void){};
    int(WINAPI* ErrorGetCode)(void){};
    DWORD(WINAPI* GetDevice)(void){};
    BOOL(WINAPI* SetConfig)(DWORD, DWORD){};
    DWORD(WINAPI* GetConfig)(DWORD){};

    HSTREAM(WINAPI* StreamCreate)(DWORD, DWORD, DWORD, STREAMPROC*, void*){};
    BOOL(WINAPI* StreamFree)(HSTREAM){};
    DWORD(WINAPI* StreamPutData)(HSTREAM, const void*, DWORD){};

    BOOL(WINAPI* RecordGetDeviceInfo)(DWORD, BASS_DEVICEINFO*){};
    BOOL(WINAPI* RecordInit)(int){};
    BOOL(WINAPI* RecordFree)(void){};
    BOOL(WINAPI* RecordSetDevice)(DWORD){};
    DWORD(WINAPI* RecordGetDevice)(void){};
    HRECORD(WINAPI* RecordStart)(DWORD, DWORD, DWORD, RECORDPROC*, void*){};

    DWORD(WINAPI* ChannelIsActive)(DWORD){};
    DWORD(WINAPI* ChannelFlags)(DWORD, DWORD, DWORD){};
    BOOL(WINAPI* ChannelPlay)(DWORD, BOOL){};
    BOOL(WINAPI* ChannelStop)(DWORD){};
    BOOL(WINAPI* ChannelPause)(DWORD){};
    BOOL(WINAPI* ChannelSetAttribute)(DWORD, DWORD, float){};
    BOOL(WINAPI* ChannelGetAttribute)(DWORD, DWORD, float*){};
    BOOL(WINAPI* ChannelSlideAttribute)(DWORD, DWORD, float, DWORD){};
    DWORD(WINAPI* ChannelGetData)(DWORD, void*, DWORD){};
    HFX(WINAPI* ChannelSetFX)(DWORD, DWORD, int){};
    BOOL(WINAPI* ChannelRemoveFX)(DWORD, HFX){};
    BOOL(WINAPI* FXSetParameters)(HFX, const void*){};
};

} // namespace svplus::client::audio
