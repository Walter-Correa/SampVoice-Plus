/*
================================================================================
    SampVoice Plus - Comprehensive Demonstration Script
    Continuation & Modernization of CyberMor/sampvoice & AmyrAhmady/sampvoice
    Author: Walter Correa (Walter-Correa)

    Features demonstrated:
    - Client version detection (Legacy SV v11 vs SampVoice Plus v12) & mic check
    - Global broadcast stream (SvCreateGStream)
    - Proximity local stream attached to player with spatial grid scaling
    - Static megaphone / speaker zone stream (SvCreateSLStreamAtPoint)
    - Push-to-talk key registration (SvAddKey: 'B' proximity, 'N' radio/global)
    - DX8 radio filter chain (Parameq + Distortion + Echo via SvEffect*)
    - Dynamic volume sliding (SvStreamParameterSlideFromTo)
    - Full resource cleanup on disconnect & gamemode exit
================================================================================
*/

#include <a_samp>
#include <sampvoice>

#define KEY_PTT_PROXIMITY   (0x42) // 'B'
#define KEY_PTT_RADIO       (0x4E) // 'N'

new SV_GSTREAM:g_RadioStream   = SV_NULL;
new SV_LPSTREAM:g_PlazaStream  = SV_NULL;
new SV_DLSTREAM:g_LocalStream[MAX_PLAYERS] = { SV_NULL, ... };

new SV_EFFECT:g_RadioEqEffect   = SV_NULL;
new SV_EFFECT:g_RadioEchoEffect = SV_NULL;

// -----------------------------------------------------------------------------
// Callback: OnGameModeInit
// -----------------------------------------------------------------------------
public OnGameModeInit()
{
    // 1. Create Global Police / Dispatch Radio Stream
    g_RadioStream = SvCreateGStream(0x3498DBFF, "Dispatch Radio");
    SvStreamSetIcon(g_RadioStream, "radio");

    // Attach DX8 walkie-talkie band-pass & light echo DSP effects to radio stream
    g_RadioEqEffect = SvEffectCreateParameq(1, 1800.0, 12.0, -6.0);
    if (g_RadioEqEffect != SV_NULL)
    {
        SvEffectAttachStream(g_RadioEqEffect, g_RadioStream);
    }

    g_RadioEchoEffect = SvEffectCreateEcho(2, 15.0, 20.0, 35.0, 35.0, SV_FALSE);
    if (g_RadioEchoEffect != SV_NULL)
    {
        SvEffectAttachStream(g_RadioEchoEffect, g_RadioStream);
    }

    // 2. Create Static Ambient / Megaphone Stream at Pershing Square (Los Santos)
    g_PlazaStream = SvCreateSLStreamAtPoint(50.0, 1480.0, -1740.0, 13.5, 0xF1C40FFF, "City Hall PA");
    SvStreamSetIcon(g_PlazaStream, "megaphone");

    printf("[SampVoice Plus] Example gamemode initialized (version %s).", SV_PLUS_VERSION_STRING);
    return 1;
}

// -----------------------------------------------------------------------------
// Callback: OnGameModeExit
// -----------------------------------------------------------------------------
public OnGameModeExit()
{
    if (g_RadioEqEffect != SV_NULL)
    {
        SvEffectDelete(g_RadioEqEffect);
        g_RadioEqEffect = SV_NULL;
    }

    if (g_RadioEchoEffect != SV_NULL)
    {
        SvEffectDelete(g_RadioEchoEffect);
        g_RadioEchoEffect = SV_NULL;
    }

    if (g_RadioStream != SV_NULL)
    {
        SvDeleteStream(g_RadioStream);
        g_RadioStream = SV_NULL;
    }

    if (g_PlazaStream != SV_NULL)
    {
        SvDeleteStream(g_PlazaStream);
        g_PlazaStream = SV_NULL;
    }
    return 1;
}

// -----------------------------------------------------------------------------
// Callback: OnPlayerConnect
// -----------------------------------------------------------------------------
public OnPlayerConnect(playerid)
{
    new version = SvGetVersion(playerid);
    if (version == SV_NULL)
    {
        SendClientMessage(playerid, 0xE67E22FF,
            "[Voice] Voice chat plugin not detected. Install sampvoice.asi to talk & listen.");
        return 1;
    }

    if (SvGetClientProtocol(playerid) >= SV_PLUS_VERSION)
    {
        SendClientMessage(playerid, 0x2ECC71FF,
            "[Voice] Connected with SampVoice Plus! Press F11 anytime for voice settings.");
    }
    else
    {
        SendClientMessage(playerid, 0x3498DBFF,
            "[Voice] Legacy SampVoice client detected (v11 compatibility mode active).");
    }

    if (!SvHasMicro(playerid))
    {
        SendClientMessage(playerid, 0xF39C12FF,
            "[Voice] No active microphone detected; listen-only mode enabled.");
    }
    else
    {
        // Register proximity ('B') and radio ('N') activation keys
        SvAddKey(playerid, KEY_PTT_PROXIMITY);
        SvAddKey(playerid, KEY_PTT_RADIO);
    }

    // Create proximity 3D stream attached to this player (35.0m hearing radius, up to 24 concurrent listeners)
    g_LocalStream[playerid] = SvCreateDLStreamAtPlayer(35.0, 24, playerid, 0xFFFFFFFF, "Local");

    // Subscribe player to global radio channel
    if (g_RadioStream != SV_NULL)
    {
        SvAttachListenerToStream(g_RadioStream, playerid);
    }
    return 1;
}

// -----------------------------------------------------------------------------
// Callback: OnPlayerVoiceReady
// -----------------------------------------------------------------------------
public SV_VOID:OnPlayerVoiceReady(SV_UINT:playerid)
{
    SendClientMessage(playerid, 0x2ECC71FF, "[Voice] Voice connected.");
    if (SvGetClientProtocol(playerid) >= SV_PLUS_VERSION &&
        SvGetPlusClientVersion(playerid) < SV_PLUS_VERSION_NUMBER)
    {
        SendClientMessage(playerid, 0xF39C12FF,
            "[Voice] A newer version of SampVoice Plus client is available.");
    }
}

// -----------------------------------------------------------------------------
// Callback: OnPlayerDisconnect
// -----------------------------------------------------------------------------
public OnPlayerDisconnect(playerid, reason)
{
    if (g_LocalStream[playerid] != SV_NULL)
    {
        SvDeleteStream(g_LocalStream[playerid]);
        g_LocalStream[playerid] = SV_NULL;
    }

    if (g_RadioStream != SV_NULL)
    {
        SvDetachSpeakerFromStream(g_RadioStream, playerid);
        SvDetachListenerFromStream(g_RadioStream, playerid);
    }
    return 1;
}

// -----------------------------------------------------------------------------
// Callbacks: Push-To-Talk Key Press & Release
// -----------------------------------------------------------------------------
public SV_VOID:OnPlayerActivationKeyPress(SV_UINT:playerid, SV_UINT:keyid)
{
    if (SvMutePlayerStatus(playerid))
    {
        return;
    }

    if (keyid == KEY_PTT_PROXIMITY && g_LocalStream[playerid] != SV_NULL)
    {
        // Smooth volume fade-in over 120 ms when key is pressed
        SvStreamParameterSlideFromTo(g_LocalStream[playerid], SV_PARAMETER_VOLUME, 0.25, 1.0, 120);
        SvAttachSpeakerToStream(g_LocalStream[playerid], playerid);
    }
    else if (keyid == KEY_PTT_RADIO && g_RadioStream != SV_NULL)
    {
        SvAttachSpeakerToStream(g_RadioStream, playerid);
    }
}

public SV_VOID:OnPlayerActivationKeyRelease(SV_UINT:playerid, SV_UINT:keyid)
{
    if (keyid == KEY_PTT_PROXIMITY && g_LocalStream[playerid] != SV_NULL)
    {
        SvDetachSpeakerFromStream(g_LocalStream[playerid], playerid);
    }
    else if (keyid == KEY_PTT_RADIO && g_RadioStream != SV_NULL)
    {
        SvDetachSpeakerFromStream(g_RadioStream, playerid);
    }
}
