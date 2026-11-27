/*
================================================================================
    SampVoice Plus - Comprehensive Interactive Demonstration Filterscript
    Version target: SampVoice Plus 1.4.0+ (requires sampvoice.inc)

    What this filterscript demonstrates in real time (/voice or /voip):
    - Client diagnostics: v11/v12 protocol, build version, capabilities, latency
    - Proximity chat (DL/SL on player), distance updates, icons, live name/color
    - Radio channels (shared global streams, DX8 presets, per-listener volume)
    - 1-on-1 phone call (call invite/accept/decline, SvStartRecord / SvStopRecord)
    - Megaphone (high-range local stream + distortion effect)
    - Vehicle intercom (SL vs DL vehicle stream + live passenger attach/detach)
    - Speaker box (point stream, live position move, VW/interior filtering, kick)
    - Object sound (DL/SL on standard GTA object with MoveObject tracking)
    - Parameters lab (volume, pan, pitch/frequency 0.0 original, slides, GetEx)
    - Effects lab (all 9 DX8 effects: Create, Attach, Update presets, Delete)
    - Privacy & moderation (pair-mute, listener toggle, RCON timed server-mute)
    - Stream explorer (live property inspection + listener/speaker name lists)
    - Live callback feed (rate-limited chat display for all 8 voice callbacks)

    Loading on SA-MP (server.cfg):
      filterscripts FS_SampVoicePlus_Demo

    Loading on open.mp (config.json):
      "pawn": { "side_scripts": [ "filterscripts/FS_SampVoicePlus_Demo" ] }

    Loading at runtime via RCON:
      rcon loadfs FS_SampVoicePlus_Demo

    Commands & Default Activation Keys:
      /voice or /voip  -> Open the interactive feature menu
      B (0x42)         -> Proximity / Vehicle intercom / Point / Object PTT
      N (0x4E)         -> Active Radio channel / Phone call PTT
      M (0x4D)         -> Megaphone PTT
================================================================================
*/

#tryinclude <open.mp>
#if !defined _INC_open_mp
    #include <a_samp>
#endif
#include <sampvoice>

// =============================================================================
// 1. Configuration
// =============================================================================

#if !defined SVD_DIALOG_BASE
    #define SVD_DIALOG_BASE         24100
#endif

#if !defined SVD_KEY_LOCAL
    #define SVD_KEY_LOCAL           (0x42) // 'B' (also fixed Android launcher key)
#endif

#if !defined SVD_KEY_RADIO
    #define SVD_KEY_RADIO           (0x4E) // 'N'
#endif

#if !defined SVD_KEY_MEGA
    #define SVD_KEY_MEGA            (0x4D) // 'M'
#endif

// =============================================================================
// 2. Constants
// =============================================================================

#define SVD_MAX_RADIO_CHANNELS      3
#define SVD_MAX_EVENTS_PER_SEC      4
#define SVD_BOOMBOX_MODEL           2226
#define SVD_SPEAKER_OBJ_MODEL       1840

#define SVD_COLOR_INFO              0x2ECC71FF
#define SVD_COLOR_WARN              0xF39C12FF
#define SVD_COLOR_ERR               0xE74C3CFF
#define SVD_COLOR_FEED              0x85C1E9FF

#define SVD_MUTE_WORDS              ((MAX_PLAYERS + 31) / 32)

enum
{
    SVD_DLG_MAIN = SVD_DIALOG_BASE,
    SVD_DLG_STATUS,
    SVD_DLG_PROXIMITY,
    SVD_DLG_RADIO_LIST,
    SVD_DLG_RADIO_DETAIL,
    SVD_DLG_PHONE,
    SVD_DLG_PHONE_PICK,
    SVD_DLG_PHONE_INCOMING,
    SVD_DLG_MEGAPHONE,
    SVD_DLG_INTERCOM,
    SVD_DLG_SPEAKERBOX,
    SVD_DLG_OBJSOUND,
    SVD_DLG_PARAMS,
    SVD_DLG_PARAMS_PICK_STREAM,
    SVD_DLG_EFFECTS,
    SVD_DLG_EFFECTS_ITEM,
    SVD_DLG_EFFECTS_PICK_STREAM,
    SVD_DLG_PRIVACY,
    SVD_DLG_PRIVACY_PAIR_INPUT,
    SVD_DLG_PRIVACY_MUTE_INPUT,
    SVD_DLG_EXPLORER,
    SVD_DLG_EXPLORER_DETAIL,
    SVD_DLG_LIVE_EVENTS,
    SVD_DLG_ABOUT,
    SVD_DLG_KEYBINDS,
    SVD_DLG_SERVER_STATS
}

enum
{
    SVD_LAB_STREAM_PROX = 0,
    SVD_LAB_STREAM_MEGA,
    SVD_LAB_STREAM_BOX,
    SVD_LAB_STREAM_OBJ,
    SVD_LAB_STREAM_VEH,
    SVD_LAB_STREAM_RADIO0,
    SVD_LAB_STREAM_RADIO1,
    SVD_LAB_STREAM_RADIO2,
    SVD_LAB_STREAM_PHONE
}

enum
{
    SVD_FX_CHORUS = 0,
    SVD_FX_COMPRESSOR,
    SVD_FX_DISTORTION,
    SVD_FX_ECHO,
    SVD_FX_FLANGER,
    SVD_FX_GARGLE,
    SVD_FX_I3DL2,
    SVD_FX_PARAMEQ,
    SVD_FX_REVERB,
    SVD_FX_COUNT
}

// =============================================================================
// 3. Per-Player State
// =============================================================================

static bool:gSvdWelcomed[MAX_PLAYERS];

// Keys physically added by this FS (never touch keys already owned by the GM)
static bool:gSvdOwnsKeyB[MAX_PLAYERS];
static bool:gSvdOwnsKeyN[MAX_PLAYERS];
static bool:gSvdOwnsKeyM[MAX_PLAYERS];

// Proximity chat
static SV_STREAM:gSvdProxStream[MAX_PLAYERS];
static bool:gSvdProxSlMode[MAX_PLAYERS];
static Float:gSvdProxDist[MAX_PLAYERS];
static gSvdProxColorIdx[MAX_PLAYERS];
static gSvdProxIconIdx[MAX_PLAYERS];
static gSvdProxNameIdx[MAX_PLAYERS];

// Radio membership (-1 = none, 0..2 = channel)
static gSvdRadioChan[MAX_PLAYERS];
static gSvdActiveRadioUi[MAX_PLAYERS];

// 1-on-1 Phone call
static gSvdCallPeer[MAX_PLAYERS];          // Connected peer or INVALID_PLAYER_ID
static gSvdCallIncomingFrom[MAX_PLAYERS];  // Pending caller or INVALID_PLAYER_ID
static gSvdCallIncomingAt[MAX_PLAYERS];    // gettime() timestamp of pending invite (30 s TTL)
static SV_GSTREAM:gSvdCallStream[MAX_PLAYERS]; // Shared handle stored on both peers
static bool:gSvdWeStartedRecord[MAX_PLAYERS];
static bool:gSvdAcceptCalls[MAX_PLAYERS];  // Opt-in demo call invites (default OFF)
static gSvdLastCallSentAt[MAX_PLAYERS];    // gettime() cooldown between outgoing invites

// Megaphone
static SV_DLSTREAM:gSvdMegaStream[MAX_PLAYERS];
static SV_EFFECT:gSvdMegaFx[MAX_PLAYERS];

// Vehicle intercom
static bool:gSvdIntercomEnabled[MAX_PLAYERS];
static gSvdIntercomVehId[MAX_PLAYERS];
static SV_STREAM:gSvdIntercomStream[MAX_PLAYERS];
static bool:gSvdIntercomDlMode[MAX_PLAYERS];

// Speaker box (per-player point demo)
static SV_LPSTREAM:gSvdBoxStream[MAX_PLAYERS];
static bool:gSvdBoxSlMode[MAX_PLAYERS];
static bool:gSvdBoxWorldFiltered[MAX_PLAYERS];
static gSvdBoxObj[MAX_PLAYERS];

// Object sound (per-player standard GTA object demo)
static SV_STREAM:gSvdObjStream[MAX_PLAYERS];
static bool:gSvdObjSlMode[MAX_PLAYERS];
static gSvdObjId[MAX_PLAYERS];
static bool:gSvdObjMoved[MAX_PLAYERS];
static Float:gSvdObjOriginX[MAX_PLAYERS];
static Float:gSvdObjOriginY[MAX_PLAYERS];
static Float:gSvdObjOriginZ[MAX_PLAYERS];

// Parameters & Effects Lab target selection
static gSvdLabTarget[MAX_PLAYERS];
static SV_EFFECT:gSvdLabFx[MAX_PLAYERS][SVD_FX_COUNT];
static bool:gSvdLabFxStrong[MAX_PLAYERS][SVD_FX_COUNT];
static gSvdSelectedLabFx[MAX_PLAYERS];
static gSvdSelectedExplorerIdx[MAX_PLAYERS];

// Privacy & moderation tracking (only clean what this FS changed)
static gSvdPairMutedBits[MAX_PLAYERS][SVD_MUTE_WORDS];
static bool:gSvdFsDisabledListener[MAX_PLAYERS];
static bool:gSvdFsServerMuted[MAX_PLAYERS];

// Live callback feed
static bool:gSvdFeedSelf[MAX_PLAYERS];
static bool:gSvdFeedAll[MAX_PLAYERS]; // RCON admin option
static gSvdFeedSec[MAX_PLAYERS];
static gSvdFeedCount[MAX_PLAYERS];

// =============================================================================
// 4. Shared State
// =============================================================================

static SV_GSTREAM:gSvdRadioStream[SVD_MAX_RADIO_CHANNELS];
static bool:gSvdRadioFxOn[SVD_MAX_RADIO_CHANNELS];
static gSvdRadioNoiseLevel[SVD_MAX_RADIO_CHANNELS]; // 0=low, 1=medium, 2=high
static SV_EFFECT:gSvdRadioParameqFx[SVD_MAX_RADIO_CHANNELS];
static SV_EFFECT:gSvdRadioDistFx[SVD_MAX_RADIO_CHANNELS];

static bool:gSvdFsToggledDebug = false;

// Single global dialog text scratch buffer (avoids local stack spikes; no #pragma dynamic needed)
static gSvdDlg[2048];
static SV_STREAM:gSvdTmpStreams[32];

// =============================================================================
// 5. Utilities
// =============================================================================

static stock SvdCopyConst(dest[], const src[], size)
{
    new i = 0;
    while (i + 1 < size && src[i] != '\0')
    {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

#if defined _INC_open_mp
static stock SvdShowDlg(playerid, dialogid, DIALOG_STYLE:style, const caption[], const body[], const btn1[], const btn2[])
{
    ShowPlayerDialog(playerid, dialogid, style, caption, body, btn1, btn2);
}
#else
static stock SvdShowDlg(playerid, dialogid, style, const caption[], body[], const btn1[], const btn2[])
{
    new cap[64], b1[20], b2[20];
    SvdCopyConst(cap, caption, sizeof(cap));
    SvdCopyConst(b1, btn1, sizeof(b1));
    SvdCopyConst(b2, btn2, sizeof(b2));
    ShowPlayerDialog(playerid, dialogid, style, cap, body, b1, b2);
}
#endif

static stock SvdNotifyErr(playerid, const nativeName[])
{
    new msg[128];
    format(msg, sizeof(msg), "{E74C3C}[SV+ Demo]{FFFFFF} Native {F39C12}%s{FFFFFF} failed or rejected the operation.", nativeName);
    SendClientMessage(playerid, -1, msg);
}

static stock SvdGetStreamTypeLabel(SV_STREAM:stream, out[], size = sizeof out)
{
    switch (SvGetStreamType(stream))
    {
        case SV_STREAM_TYPE_GLOBAL:     format(out, size, "GLOBAL");
        case SV_STREAM_TYPE_SL_POINT:   format(out, size, "SL_POINT");
        case SV_STREAM_TYPE_SL_VEHICLE: format(out, size, "SL_VEHICLE");
        case SV_STREAM_TYPE_SL_PLAYER:  format(out, size, "SL_PLAYER");
        case SV_STREAM_TYPE_SL_OBJECT:  format(out, size, "SL_OBJECT");
        case SV_STREAM_TYPE_DL_POINT:   format(out, size, "DL_POINT");
        case SV_STREAM_TYPE_DL_VEHICLE: format(out, size, "DL_VEHICLE");
        case SV_STREAM_TYPE_DL_PLAYER:  format(out, size, "DL_PLAYER");
        case SV_STREAM_TYPE_DL_OBJECT:  format(out, size, "DL_OBJECT");
        default:                        format(out, size, "INVALID");
    }
}

static stock SvdFormatPlusVer(verNum, out[], size = sizeof out)
{
    if (verNum <= 0)
    {
        format(out, size, "n/a");
        return;
    }
    new maj = verNum / 10000;
    new minr = (verNum % 10000) / 100;
    new pat = verNum % 100;
    format(out, size, "%d.%d.%d", maj, minr, pat);
}

static stock SvdGetSafeName(playerid, out[], size = sizeof out)
{
    if (playerid < 0 || playerid >= MAX_PLAYERS || !IsPlayerConnected(playerid))
    {
        format(out, size, "Offline#%d", playerid);
        return;
    }
    GetPlayerName(playerid, out, size);
}

static stock bool:SvdNeedsKeyB(playerid)
{
    return (gSvdProxStream[playerid] != SV_NULL) ||
           gSvdIntercomEnabled[playerid] ||
           (gSvdBoxStream[playerid] != SV_NULL) ||
           (gSvdObjStream[playerid] != SV_NULL);
}

static stock bool:SvdNeedsKeyN(playerid)
{
    return (gSvdRadioChan[playerid] != -1) || (gSvdCallPeer[playerid] != INVALID_PLAYER_ID);
}

static stock bool:SvdNeedsKeyM(playerid)
{
    return (gSvdMegaStream[playerid] != SV_NULL);
}

// Register an activation key ONLY if the server GM has not already registered it;
// record ownership so cleanup never deletes gamemode-owned keys.
static stock SvdSyncPlayerKeys(playerid)
{
    new bool:wantB = SvdNeedsKeyB(playerid);
    if (wantB && !gSvdOwnsKeyB[playerid])
    {
        if (!SvHasKey(playerid, SVD_KEY_LOCAL))
        {
            if (SvAddKey(playerid, SVD_KEY_LOCAL))
            {
                gSvdOwnsKeyB[playerid] = true;
            }
        }
    }
    else if (!wantB && gSvdOwnsKeyB[playerid])
    {
        SvRemoveKey(playerid, SVD_KEY_LOCAL);
        gSvdOwnsKeyB[playerid] = false;
    }

    new bool:wantN = SvdNeedsKeyN(playerid);
    if (wantN && !gSvdOwnsKeyN[playerid])
    {
        if (!SvHasKey(playerid, SVD_KEY_RADIO))
        {
            if (SvAddKey(playerid, SVD_KEY_RADIO))
            {
                gSvdOwnsKeyN[playerid] = true;
            }
        }
    }
    else if (!wantN && gSvdOwnsKeyN[playerid])
    {
        SvRemoveKey(playerid, SVD_KEY_RADIO);
        gSvdOwnsKeyN[playerid] = false;
    }

    new bool:wantM = SvdNeedsKeyM(playerid);
    if (wantM && !gSvdOwnsKeyM[playerid])
    {
        if (!SvHasKey(playerid, SVD_KEY_MEGA))
        {
            if (SvAddKey(playerid, SVD_KEY_MEGA))
            {
                gSvdOwnsKeyM[playerid] = true;
            }
        }
    }
    else if (!wantM && gSvdOwnsKeyM[playerid])
    {
        SvRemoveKey(playerid, SVD_KEY_MEGA);
        gSvdOwnsKeyM[playerid] = false;
    }
}

static stock SvdSetPairBit(listenerid, speakerid, bool:on)
{
    new w = speakerid >>> 5;
    new mask = 1 << (speakerid & 31);
    if (on) gSvdPairMutedBits[listenerid][w] |= mask;
    else    gSvdPairMutedBits[listenerid][w] &= ~mask;
}

static stock bool:SvdGetPairBit(listenerid, speakerid)
{
    new w = speakerid >>> 5;
    new mask = 1 << (speakerid & 31);
    return (gSvdPairMutedBits[listenerid][w] & mask) != 0;
}

static stock SV_STREAM:SvdResolveLabStream(playerid, slot = -1)
{
    new t = (slot == -1) ? gSvdLabTarget[playerid] : slot;
    switch (t)
    {
        case SVD_LAB_STREAM_PROX:   return gSvdProxStream[playerid];
        case SVD_LAB_STREAM_MEGA:   return gSvdMegaStream[playerid];
        case SVD_LAB_STREAM_BOX:    return gSvdBoxStream[playerid];
        case SVD_LAB_STREAM_OBJ:    return gSvdObjStream[playerid];
        case SVD_LAB_STREAM_VEH:    return gSvdIntercomStream[playerid];
        case SVD_LAB_STREAM_RADIO0: return gSvdRadioStream[0];
        case SVD_LAB_STREAM_RADIO1: return gSvdRadioStream[1];
        case SVD_LAB_STREAM_RADIO2: return gSvdRadioStream[2];
        case SVD_LAB_STREAM_PHONE:  return gSvdCallStream[playerid];
    }
    return SV_NULL;
}

static stock SvdGetLabTargetLabel(playerid, out[], size = sizeof out)
{
    new SV_STREAM:st = SvdResolveLabStream(playerid);
    if (st == SV_NULL || !SvIsValidStream(st))
    {
        format(out, size, "None (enable a demo stream first)");
        return;
    }
    new sName[64];
    if (!SvStreamGetName(st, sName, sizeof(sName)) || sName[0] == '\0')
    {
        format(sName, sizeof(sName), "Unnamed");
    }
    new sType[20];
    SvdGetStreamTypeLabel(st, sType, sizeof(sType));
    format(out, size, "%s [%s]", sName, sType);
}

static stock bool:SvdParseIntStrict(const str[], &val)
{
    if (str[0] == '\0') return false;
    new i = 0;
    while (str[i] != '\0')
    {
        if (str[i] < '0' || str[i] > '9') return false;
        i++;
    }
    val = strval(str);
    return true;
}

// Reset per-player arrays safely (also used when FS hot-loads with connected players)
static stock SvdInitPlayerVars(playerid)
{
    gSvdWelcomed[playerid] = false;
    gSvdOwnsKeyB[playerid] = false;
    gSvdOwnsKeyN[playerid] = false;
    gSvdOwnsKeyM[playerid] = false;

    gSvdProxStream[playerid] = SV_NULL;
    gSvdProxSlMode[playerid] = false;
    gSvdProxDist[playerid] = 35.0;
    gSvdProxColorIdx[playerid] = 0;
    gSvdProxIconIdx[playerid] = 0;
    gSvdProxNameIdx[playerid] = 0;

    gSvdRadioChan[playerid] = -1;
    gSvdActiveRadioUi[playerid] = 0;

    gSvdCallPeer[playerid] = INVALID_PLAYER_ID;
    gSvdCallIncomingFrom[playerid] = INVALID_PLAYER_ID;
    gSvdCallIncomingAt[playerid] = 0;
    gSvdCallStream[playerid] = SV_NULL;
    gSvdWeStartedRecord[playerid] = false;
    gSvdAcceptCalls[playerid] = false;
    gSvdLastCallSentAt[playerid] = 0;

    gSvdMegaStream[playerid] = SV_NULL;
    gSvdMegaFx[playerid] = SV_NULL;

    gSvdIntercomEnabled[playerid] = false;
    gSvdIntercomVehId[playerid] = 0;
    gSvdIntercomStream[playerid] = SV_NULL;
    gSvdIntercomDlMode[playerid] = false;

    gSvdBoxStream[playerid] = SV_NULL;
    gSvdBoxSlMode[playerid] = false;
    gSvdBoxWorldFiltered[playerid] = false;
    gSvdBoxObj[playerid] = INVALID_OBJECT_ID;

    gSvdObjStream[playerid] = SV_NULL;
    gSvdObjSlMode[playerid] = false;
    gSvdObjId[playerid] = INVALID_OBJECT_ID;
    gSvdObjMoved[playerid] = false;

    gSvdLabTarget[playerid] = SVD_LAB_STREAM_PROX;
    for (new e = 0; e < SVD_FX_COUNT; e++)
    {
        gSvdLabFx[playerid][e] = SV_NULL;
        gSvdLabFxStrong[playerid][e] = false;
    }
    gSvdSelectedLabFx[playerid] = 0;
    gSvdSelectedExplorerIdx[playerid] = 0;

    for (new w = 0; w < SVD_MUTE_WORDS; w++)
    {
        gSvdPairMutedBits[playerid][w] = 0;
    }
    gSvdFsDisabledListener[playerid] = false;
    gSvdFsServerMuted[playerid] = false;

    gSvdFeedSelf[playerid] = false;
    gSvdFeedAll[playerid] = false;
    gSvdFeedSec[playerid] = 0;
    gSvdFeedCount[playerid] = 0;
}

// Rate-limited event broadcaster for item 13 (Live events)
static stock SvdEmitEvent(subjectId, const eventText[])
{
    new msg[144];
    new nowSec = gettime();
    for (new i = 0; i < MAX_PLAYERS; i++)
    {
        if (!IsPlayerConnected(i)) continue;
        new bool:want = false;
        if (i == subjectId && gSvdFeedSelf[i]) want = true;
        else if (gSvdFeedAll[i] && IsPlayerAdmin(i)) want = true;

        if (!want) continue;

        if (gSvdFeedSec[i] != nowSec)
        {
            gSvdFeedSec[i] = nowSec;
            gSvdFeedCount[i] = 0;
        }
        if (gSvdFeedCount[i] >= SVD_MAX_EVENTS_PER_SEC)
        {
            continue;
        }
        gSvdFeedCount[i]++;
        format(msg, sizeof(msg), "{85C1E9}[SV Event]{FFFFFF} %s", eventText);
        SendClientMessage(i, -1, msg);
    }
}

// =============================================================================
// 6. Demo Subsystems (Creation, Updates & Teardown)
// =============================================================================

// --- Demo 2: Proximity Chat ---

static stock SvdApplyProxStyle(playerid)
{
    if (gSvdProxStream[playerid] == SV_NULL) return;
    static const sColors[4] = { 0xFFFFFFFF, 0x2ECC71FF, 0x3498DBFF, 0xF39C12FF };
    static const sNames[3][16] = { "Proximity", "Nearby Chat", "Whisper Zone" };
    static const sIcons[3][16] = { "speaker", "global", "admin" };

    SvStreamSetColor(gSvdProxStream[playerid], sColors[gSvdProxColorIdx[playerid] % 4]);
    SvStreamSetName(gSvdProxStream[playerid], sNames[gSvdProxNameIdx[playerid] % 3]);
    SvStreamSetIcon(gSvdProxStream[playerid], sIcons[gSvdProxIconIdx[playerid] % 3]);
}

static stock SvdToggleProximity(playerid, bool:enable)
{
    if (enable)
    {
        if (gSvdProxStream[playerid] != SV_NULL)
        {
            SvDeleteStream(gSvdProxStream[playerid]);
            gSvdProxStream[playerid] = SV_NULL;
        }
        if (gSvdProxSlMode[playerid])
        {
            // SL on player: only explicitly attached listeners hear this 3D source
            gSvdProxStream[playerid] = SvCreateSLStreamAtPlayer(gSvdProxDist[playerid], playerid, 0xFFFFFFFF, "Proximity");
            if (gSvdProxStream[playerid] != SV_NULL)
            {
                for (new p = 0; p < MAX_PLAYERS; p++)
                {
                    if (IsPlayerConnected(p) && p != playerid)
                    {
                        SvAttachListenerToStream(gSvdProxStream[playerid], p);
                    }
                }
            }
        }
        else
        {
            // DL on player: spatial grid selects up to 24 nearest listeners automatically
            gSvdProxStream[playerid] = SvCreateDLStreamAtPlayer(gSvdProxDist[playerid], 24, playerid, 0xFFFFFFFF, "Proximity");
        }
        if (gSvdProxStream[playerid] == SV_NULL)
        {
            if (gSvdProxSlMode[playerid]) SvdNotifyErr(playerid, "SvCreateSLStreamAtPlayer");
            else SvdNotifyErr(playerid, "SvCreateDLStreamAtPlayer");
        }
        else
        {
            SvdApplyProxStyle(playerid);
        }
    }
    else
    {
        if (gSvdProxStream[playerid] != SV_NULL)
        {
            SvDeleteStream(gSvdProxStream[playerid]);
            gSvdProxStream[playerid] = SV_NULL;
        }
    }
    SvdSyncPlayerKeys(playerid);
}

// --- Demo 3: Radio Channels (3 Shared Global Streams) ---

static stock SvdEnsureRadioChannel(chanIdx)
{
    if (chanIdx < 0 || chanIdx >= SVD_MAX_RADIO_CHANNELS) return;
    if (gSvdRadioStream[chanIdx] != SV_NULL && SvIsValidStream(gSvdRadioStream[chanIdx])) return;

    static const sChanNames[SVD_MAX_RADIO_CHANNELS][24] = { "Alpha Radio", "Bravo Dispatch", "Charlie Squad" };
    static const sChanColors[SVD_MAX_RADIO_CHANNELS] = { 0x3498DBFF, 0x2ECC71FF, 0x9B59B6FF };

    gSvdRadioStream[chanIdx] = SvCreateGStream(sChanColors[chanIdx], sChanNames[chanIdx]);
    if (gSvdRadioStream[chanIdx] != SV_NULL)
    {
        SvStreamSetIcon(gSvdRadioStream[chanIdx], "radio");
    }
}

static stock SvdApplyRadioFilter(chanIdx, bool:enable, noiseLevel)
{
    SvdEnsureRadioChannel(chanIdx);
    new SV_GSTREAM:st = gSvdRadioStream[chanIdx];
    if (st == SV_NULL) return;

    gSvdRadioFxOn[chanIdx] = enable;
    gSvdRadioNoiseLevel[chanIdx] = noiseLevel;

    if (enable)
    {
        if (gSvdRadioParameqFx[chanIdx] == SV_NULL)
        {
            gSvdRadioParameqFx[chanIdx] = SvEffectCreateParameq(1, 1800.0, 12.0, 5.0);
            if (gSvdRadioParameqFx[chanIdx] != SV_NULL)
            {
                SvEffectAttachStream(gSvdRadioParameqFx[chanIdx], st);
            }
        }
        new Float:edge = 8.0, Float:gain = -12.0;
        if (noiseLevel == 1) { edge = 20.0; gain = -8.0; }
        else if (noiseLevel == 2) { edge = 42.0; gain = -4.0; }

        if (gSvdRadioDistFx[chanIdx] == SV_NULL)
        {
            gSvdRadioDistFx[chanIdx] = SvEffectCreateDistortion(2, gain, edge, 2200.0, 1600.0, 3400.0);
            if (gSvdRadioDistFx[chanIdx] != SV_NULL)
            {
                SvEffectAttachStream(gSvdRadioDistFx[chanIdx], st);
            }
        }
        else
        {
            // Live in-place effect update without recreating the stream (Plus 1.3.0+)
            SvEffectUpdateDistortion(gSvdRadioDistFx[chanIdx], gain, edge, 2200.0, 1600.0, 3400.0);
        }
    }
    else
    {
        if (gSvdRadioParameqFx[chanIdx] != SV_NULL)
        {
            SvEffectDetachStream(gSvdRadioParameqFx[chanIdx], st);
            SvEffectDelete(gSvdRadioParameqFx[chanIdx]);
            gSvdRadioParameqFx[chanIdx] = SV_NULL;
        }
        if (gSvdRadioDistFx[chanIdx] != SV_NULL)
        {
            SvEffectDetachStream(gSvdRadioDistFx[chanIdx], st);
            SvEffectDelete(gSvdRadioDistFx[chanIdx]);
            gSvdRadioDistFx[chanIdx] = SV_NULL;
        }
    }
}

static stock SvdJoinRadioChannel(playerid, chanIdx)
{
    if (gSvdRadioChan[playerid] != -1 && gSvdRadioStream[gSvdRadioChan[playerid]] != SV_NULL)
    {
        new SV_GSTREAM:oldSt = gSvdRadioStream[gSvdRadioChan[playerid]];
        if (SvIsKeyBoundToStream(playerid, SVD_KEY_RADIO, oldSt))
        {
            SvUnbindKeyFromStream(playerid, SVD_KEY_RADIO, oldSt);
        }
        SvDetachSpeakerFromStream(oldSt, playerid);
        SvDetachListenerFromStream(oldSt, playerid);
    }
    gSvdRadioChan[playerid] = -1;

    if (chanIdx >= 0 && chanIdx < SVD_MAX_RADIO_CHANNELS)
    {
        SvdEnsureRadioChannel(chanIdx);
        if (gSvdRadioStream[chanIdx] != SV_NULL)
        {
            SvAttachListenerToStream(gSvdRadioStream[chanIdx], playerid);
            gSvdRadioChan[playerid] = chanIdx;
        }
        else
        {
            SvdNotifyErr(playerid, "SvCreateGStream");
        }
    }
    SvdSyncPlayerKeys(playerid);
    if (gSvdRadioChan[playerid] >= 0 && gSvdOwnsKeyN[playerid])
    {
        new SV_GSTREAM:newSt = gSvdRadioStream[gSvdRadioChan[playerid]];
        if (newSt != SV_NULL)
        {
            SvBindKeyToStream(playerid, SVD_KEY_RADIO, newSt);
        }
    }
}

// --- Demo 4: Phone Call (SvStartRecord / SvStopRecord) ---

static stock SvdEndPhoneCall(playerid, const reasonText[], bool:notifySelf = true)
{
    new peer = gSvdCallPeer[playerid];
    new SV_GSTREAM:callSt = gSvdCallStream[playerid];
    if (peer == INVALID_PLAYER_ID && callSt == SV_NULL && !gSvdWeStartedRecord[playerid])
    {
        return;
    }

    if (gSvdWeStartedRecord[playerid])
    {
        SvStopRecord(playerid);
        gSvdWeStartedRecord[playerid] = false;
    }
    gSvdCallPeer[playerid] = INVALID_PLAYER_ID;
    gSvdCallStream[playerid] = SV_NULL;
    SvdSyncPlayerKeys(playerid);

    if (peer != INVALID_PLAYER_ID)
    {
        if (gSvdWeStartedRecord[peer])
        {
            SvStopRecord(peer);
            gSvdWeStartedRecord[peer] = false;
        }
        gSvdCallPeer[peer] = INVALID_PLAYER_ID;
        gSvdCallStream[peer] = SV_NULL;
        SvdSyncPlayerKeys(peer);
        if (IsPlayerConnected(peer) && reasonText[0] != '\0')
        {
            SendClientMessage(peer, -1, reasonText);
        }
    }
    if (notifySelf && IsPlayerConnected(playerid) && reasonText[0] != '\0')
    {
        SendClientMessage(playerid, -1, reasonText);
    }
    if (callSt != SV_NULL && SvIsValidStream(callSt))
    {
        SvDeleteStream(callSt);
    }
}

static stock SvdAcceptPhoneCall(calleeid, callerid)
{
    if (!IsPlayerConnected(calleeid) || gSvdCallPeer[calleeid] != INVALID_PLAYER_ID)
    {
        if (IsPlayerConnected(calleeid))
        {
            SendClientMessage(calleeid, -1, "{F39C12}[Phone]{FFFFFF} You are already in an active call.");
        }
        if (IsPlayerConnected(callerid))
        {
            SendClientMessage(callerid, -1, "{F39C12}[Phone]{FFFFFF} That player is already in another call.");
        }
        return;
    }
    if (!IsPlayerConnected(callerid) || gSvdCallPeer[callerid] != INVALID_PLAYER_ID)
    {
        SendClientMessage(calleeid, -1, "{E74C3C}[Phone]{FFFFFF} The caller is no longer available.");
        return;
    }

    new nameA[24], nameB[24], stName[64];
    SvdGetSafeName(callerid, nameA, sizeof(nameA));
    SvdGetSafeName(calleeid, nameB, sizeof(nameB));
    format(stName, sizeof(stName), "Call: %s <-> %s", nameA, nameB);

    new SV_GSTREAM:callSt = SvCreateGStream(0x2ECC71FF, stName);
    if (callSt == SV_NULL)
    {
        SvdNotifyErr(calleeid, "SvCreateGStream");
        return;
    }
    SvStreamSetIcon(callSt, "phone");
    SvAttachListenerToStream(callSt, callerid);
    SvAttachListenerToStream(callSt, calleeid);
    SvAttachSpeakerToStream(callSt, callerid);
    SvAttachSpeakerToStream(callSt, calleeid);

    gSvdCallPeer[callerid] = calleeid;
    gSvdCallPeer[calleeid] = callerid;
    gSvdCallStream[callerid] = callSt;
    gSvdCallStream[calleeid] = callSt;

    // SvStartRecord opens the microphone automatically without holding a PTT key
    if (SvStartRecord(callerid)) gSvdWeStartedRecord[callerid] = true;
    if (SvStartRecord(calleeid)) gSvdWeStartedRecord[calleeid] = true;

    SvdSyncPlayerKeys(callerid);
    SvdSyncPlayerKeys(calleeid);

    SendClientMessage(callerid, -1, "{2ECC71}[Phone]{FFFFFF} Call connected! Microphone opened via SvStartRecord (N also transmits).");
    SendClientMessage(calleeid, -1, "{2ECC71}[Phone]{FFFFFF} Call connected! Microphone opened via SvStartRecord (N also transmits).");
}

// --- Demo 5: Megaphone ---

static stock SvdToggleMegaphone(playerid, bool:enable)
{
    if (gSvdMegaFx[playerid] != SV_NULL)
    {
        if (gSvdMegaStream[playerid] != SV_NULL)
        {
            SvEffectDetachStream(gSvdMegaFx[playerid], gSvdMegaStream[playerid]);
        }
        SvEffectDelete(gSvdMegaFx[playerid]);
        gSvdMegaFx[playerid] = SV_NULL;
    }
    if (gSvdMegaStream[playerid] != SV_NULL)
    {
        SvDeleteStream(gSvdMegaStream[playerid]);
        gSvdMegaStream[playerid] = SV_NULL;
    }

    if (enable)
    {
        gSvdMegaStream[playerid] = SvCreateDLStreamAtPlayer(95.0, -1, playerid, 0xF1C40FFF, "Megaphone");
        if (gSvdMegaStream[playerid] == SV_NULL)
        {
            SvdNotifyErr(playerid, "SvCreateDLStreamAtPlayer");
        }
        else
        {
            SvStreamSetIcon(gSvdMegaStream[playerid], "megaphone");
            gSvdMegaFx[playerid] = SvEffectCreateDistortion(1, -6.0, 26.0, 2400.0, 1800.0, 3600.0);
            if (gSvdMegaFx[playerid] != SV_NULL)
            {
                SvEffectAttachStream(gSvdMegaFx[playerid], gSvdMegaStream[playerid]);
            }
        }
    }
    SvdSyncPlayerKeys(playerid);
}

// --- Demo 6: Vehicle Intercom (SL at vehicle by default, optional DL mode) ---

static stock SvdStopIntercom(playerid)
{
    if (gSvdIntercomStream[playerid] != SV_NULL)
    {
        SvDeleteStream(gSvdIntercomStream[playerid]);
        gSvdIntercomStream[playerid] = SV_NULL;
    }
    gSvdIntercomEnabled[playerid] = false;
    gSvdIntercomVehId[playerid] = 0;
    SvdSyncPlayerKeys(playerid);
}

static stock SvdStartIntercom(playerid)
{
    SvdStopIntercom(playerid);
    new vehId = GetPlayerVehicleID(playerid);
    if (vehId <= 0)
    {
        SendClientMessage(playerid, -1, "{F39C12}[Intercom]{FFFFFF} Enter a vehicle first.");
        return;
    }

    if (gSvdIntercomDlMode[playerid])
    {
        gSvdIntercomStream[playerid] = SvCreateDLStreamAtVehicle(35.0, -1, vehId, 0xE67E22FF, "Vehicle PA");
    }
    else
    {
        // SL: only players explicitly attached by script (current vehicle occupants) hear it
        gSvdIntercomStream[playerid] = SvCreateSLStreamAtVehicle(30.0, vehId, 0x1ABC9CFF, "Veh Intercom");
        if (gSvdIntercomStream[playerid] != SV_NULL)
        {
            for (new p = 0; p < MAX_PLAYERS; p++)
            {
                if (IsPlayerConnected(p) && p != playerid && GetPlayerVehicleID(p) == vehId)
                {
                    SvAttachListenerToStream(gSvdIntercomStream[playerid], p);
                }
            }
        }
    }

    if (gSvdIntercomStream[playerid] == SV_NULL)
    {
        if (gSvdIntercomDlMode[playerid]) SvdNotifyErr(playerid, "SvCreateDLStreamAtVehicle");
        else SvdNotifyErr(playerid, "SvCreateSLStreamAtVehicle");
        return;
    }
    SvStreamSetIcon(gSvdIntercomStream[playerid], "police");
    gSvdIntercomEnabled[playerid] = true;
    gSvdIntercomVehId[playerid] = vehId;
    SvdSyncPlayerKeys(playerid);
}

// --- Demo 7: Speaker Box (DL/SL Point Stream) ---

static stock SvdDestroySpeakerBox(playerid)
{
    if (gSvdBoxStream[playerid] != SV_NULL)
    {
        SvDeleteStream(gSvdBoxStream[playerid]);
        gSvdBoxStream[playerid] = SV_NULL;
    }
    if (gSvdBoxObj[playerid] != INVALID_OBJECT_ID)
    {
        DestroyObject(gSvdBoxObj[playerid]);
        gSvdBoxObj[playerid] = INVALID_OBJECT_ID;
    }
    SvdSyncPlayerKeys(playerid);
}

static stock SvdPlaceSpeakerBox(playerid)
{
    SvdDestroySpeakerBox(playerid);
    new Float:x, Float:y, Float:z;
    GetPlayerPos(playerid, x, y, z);
    new vw = GetPlayerVirtualWorld(playerid);
    new intid = GetPlayerInterior(playerid);
    new worldArg = gSvdBoxWorldFiltered[playerid] ? vw : -1;
    new intArg = gSvdBoxWorldFiltered[playerid] ? intid : -1;

    if (gSvdBoxSlMode[playerid])
    {
        gSvdBoxStream[playerid] = SvCreateSLStreamAtPoint(30.0, x, y, z, 0xE84393FF, "Speaker Box (SL)");
        if (gSvdBoxStream[playerid] != SV_NULL)
        {
            for (new p = 0; p < MAX_PLAYERS; p++)
            {
                if (IsPlayerConnected(p) && p != playerid)
                {
                    SvAttachListenerToStream(gSvdBoxStream[playerid], p);
                }
            }
        }
    }
    else
    {
        gSvdBoxStream[playerid] = SvCreateDLStreamAtPoint(30.0, -1, x, y, z, 0xE84393FF, "Speaker Box", worldArg, intArg);
    }

    if (gSvdBoxStream[playerid] == SV_NULL)
    {
        if (gSvdBoxSlMode[playerid]) SvdNotifyErr(playerid, "SvCreateSLStreamAtPoint");
        else SvdNotifyErr(playerid, "SvCreateDLStreamAtPoint");
        return;
    }
    SvStreamSetIcon(gSvdBoxStream[playerid], "speaker");
    gSvdBoxObj[playerid] = CreateObject(SVD_BOOMBOX_MODEL, x + 0.8, y, z - 0.65, 0.0, 0.0, 0.0);
    SvdSyncPlayerKeys(playerid);
}

// --- Demo 8: Object Sound (Standard GTA CreateObject + DL/SL AtObject) ---

static stock SvdDestroyObjSound(playerid)
{
    if (gSvdObjStream[playerid] != SV_NULL)
    {
        SvDeleteStream(gSvdObjStream[playerid]);
        gSvdObjStream[playerid] = SV_NULL;
    }
    if (gSvdObjId[playerid] != INVALID_OBJECT_ID)
    {
        DestroyObject(gSvdObjId[playerid]);
        gSvdObjId[playerid] = INVALID_OBJECT_ID;
    }
    gSvdObjMoved[playerid] = false;
    SvdSyncPlayerKeys(playerid);
}

static stock SvdCreateObjSound(playerid)
{
    SvdDestroyObjSound(playerid);
    new Float:x, Float:y, Float:z;
    GetPlayerPos(playerid, x, y, z);
    gSvdObjOriginX[playerid] = x + 1.5;
    gSvdObjOriginY[playerid] = y;
    gSvdObjOriginZ[playerid] = z - 0.4;

    new objId = CreateObject(SVD_SPEAKER_OBJ_MODEL, gSvdObjOriginX[playerid], gSvdObjOriginY[playerid], gSvdObjOriginZ[playerid], 0.0, 0.0, 0.0);
    if (objId == INVALID_OBJECT_ID)
    {
        SendClientMessage(playerid, -1, "{E74C3C}[Object Sound]{FFFFFF} Failed to create standard GTA object.");
        return;
    }

    if (gSvdObjSlMode[playerid])
    {
        gSvdObjStream[playerid] = SvCreateSLStreamAtObject(30.0, objId, 0x00CEC9FF, "Object Audio (SL)");
        if (gSvdObjStream[playerid] != SV_NULL)
        {
            for (new p = 0; p < MAX_PLAYERS; p++)
            {
                if (IsPlayerConnected(p) && p != playerid)
                {
                    SvAttachListenerToStream(gSvdObjStream[playerid], p);
                }
            }
        }
    }
    else
    {
        gSvdObjStream[playerid] = SvCreateDLStreamAtObject(30.0, -1, objId, 0x00CEC9FF, "Object Audio", -1, -1);
    }

    if (gSvdObjStream[playerid] == SV_NULL)
    {
        DestroyObject(objId);
        if (gSvdObjSlMode[playerid]) SvdNotifyErr(playerid, "SvCreateSLStreamAtObject");
        else SvdNotifyErr(playerid, "SvCreateDLStreamAtObject");
        return;
    }

    SvStreamSetIcon(gSvdObjStream[playerid], "speaker");
    gSvdObjId[playerid] = objId;
    gSvdObjMoved[playerid] = false;
    SvdSyncPlayerKeys(playerid);
}

// --- Demo 10: Effects Lab (All 9 DX8 Effects) ---

static stock SvdGetEffectLabel(fxSlot, out[], size = sizeof out)
{
    switch (fxSlot)
    {
        case SVD_FX_CHORUS:     format(out, size, "Chorus");
        case SVD_FX_COMPRESSOR: format(out, size, "Compressor");
        case SVD_FX_DISTORTION: format(out, size, "Distortion");
        case SVD_FX_ECHO:       format(out, size, "Echo");
        case SVD_FX_FLANGER:    format(out, size, "Flanger");
        case SVD_FX_GARGLE:     format(out, size, "Gargle");
        case SVD_FX_I3DL2:      format(out, size, "I3DL2 Reverb");
        case SVD_FX_PARAMEQ:    format(out, size, "ParamEQ");
        case SVD_FX_REVERB:     format(out, size, "Reverb");
        default:                format(out, size, "Unknown");
    }
}

static stock SV_EFFECT:SvdCreatePresetEffect(fxSlot, bool:strong)
{
    switch (fxSlot)
    {
        case SVD_FX_CHORUS:     return strong ? SvEffectCreateChorus(1, 65.0, 20.0, 40.0, 2.2, 1, 16.0, 3) : SvEffectCreateChorus(1, 35.0, 10.0, 20.0, 1.1, 1, 12.0, 2);
        case SVD_FX_COMPRESSOR: return strong ? SvEffectCreateCompressor(1, 3.0, 5.0, 180.0, -28.0, 6.0, 4.0) : SvEffectCreateCompressor(1, 0.0, 10.0, 200.0, -18.0, 3.0, 4.0);
        case SVD_FX_DISTORTION: return strong ? SvEffectCreateDistortion(1, -4.0, 45.0, 2400.0, 2400.0, 4000.0) : SvEffectCreateDistortion(1, -12.0, 15.0, 2400.0, 2400.0, 6000.0);
        case SVD_FX_ECHO:       return strong ? SvEffectCreateEcho(1, 55.0, 55.0, 320.0, 320.0, false) : SvEffectCreateEcho(1, 30.0, 30.0, 160.0, 160.0, false);
        case SVD_FX_FLANGER:    return strong ? SvEffectCreateFlanger(1, 70.0, 100.0, -60.0, 0.5, 1, 3.0, 2) : SvEffectCreateFlanger(1, 40.0, 60.0, -30.0, 0.25, 1, 2.0, 2);
        case SVD_FX_GARGLE:     return strong ? SvEffectCreateGargle(1, 35, 1) : SvEffectCreateGargle(1, 15, 0);
        case SVD_FX_I3DL2:      return strong ? SvEffectCreateI3dl2reverb(1, -1000, -100, 0.0, 2.8, 0.85, -1800, 0.01, 400, 0.015, 100.0, 100.0, 5000.0) : SvEffectCreateI3dl2reverb(1, -1000, -300, 0.0, 1.2, 0.70, -2600, 0.007, 150, 0.011, 80.0, 80.0, 5000.0);
        case SVD_FX_PARAMEQ:    return strong ? SvEffectCreateParameq(1, 1600.0, 8.0, -12.0) : SvEffectCreateParameq(1, 1800.0, 12.0, -5.0);
        case SVD_FX_REVERB:     return strong ? SvEffectCreateReverb(1, 0.0, -2.0, 1800.0, 0.5) : SvEffectCreateReverb(1, 0.0, -8.0, 750.0, 0.3);
    }
    return SV_NULL;
}

static stock bool:SvdUpdatePresetEffect(SV_EFFECT:fx, fxSlot, bool:strong)
{
    switch (fxSlot)
    {
        case SVD_FX_CHORUS:     return strong ? SvEffectUpdateChorus(fx, 65.0, 20.0, 40.0, 2.2, 1, 16.0, 3) : SvEffectUpdateChorus(fx, 35.0, 10.0, 20.0, 1.1, 1, 12.0, 2);
        case SVD_FX_COMPRESSOR: return strong ? SvEffectUpdateCompressor(fx, 3.0, 5.0, 180.0, -28.0, 6.0, 4.0) : SvEffectUpdateCompressor(fx, 0.0, 10.0, 200.0, -18.0, 3.0, 4.0);
        case SVD_FX_DISTORTION: return strong ? SvEffectUpdateDistortion(fx, -4.0, 45.0, 2400.0, 2400.0, 4000.0) : SvEffectUpdateDistortion(fx, -12.0, 15.0, 2400.0, 2400.0, 6000.0);
        case SVD_FX_ECHO:       return strong ? SvEffectUpdateEcho(fx, 55.0, 55.0, 320.0, 320.0, false) : SvEffectUpdateEcho(fx, 30.0, 30.0, 160.0, 160.0, false);
        case SVD_FX_FLANGER:    return strong ? SvEffectUpdateFlanger(fx, 70.0, 100.0, -60.0, 0.5, 1, 3.0, 2) : SvEffectUpdateFlanger(fx, 40.0, 60.0, -30.0, 0.25, 1, 2.0, 2);
        case SVD_FX_GARGLE:     return strong ? SvEffectUpdateGargle(fx, 35, 1) : SvEffectUpdateGargle(fx, 15, 0);
        case SVD_FX_I3DL2:      return strong ? SvEffectUpdateI3dl2reverb(fx, -1000, -100, 0.0, 2.8, 0.85, -1800, 0.01, 400, 0.015, 100.0, 100.0, 5000.0) : SvEffectUpdateI3dl2reverb(fx, -1000, -300, 0.0, 1.2, 0.70, -2600, 0.007, 150, 0.011, 80.0, 80.0, 5000.0);
        case SVD_FX_PARAMEQ:    return strong ? SvEffectUpdateParameq(fx, 1600.0, 8.0, -12.0) : SvEffectUpdateParameq(fx, 1800.0, 12.0, -5.0);
        case SVD_FX_REVERB:     return strong ? SvEffectUpdateReverb(fx, 0.0, -2.0, 1800.0, 0.5) : SvEffectUpdateReverb(fx, 0.0, -8.0, 750.0, 0.3);
    }
    return false;
}

static stock SvdDeleteAllPlayerLabEffects(playerid)
{
    for (new e = 0; e < SVD_FX_COUNT; e++)
    {
        if (gSvdLabFx[playerid][e] != SV_NULL)
        {
            SvEffectDelete(gSvdLabFx[playerid][e]);
            gSvdLabFx[playerid][e] = SV_NULL;
        }
        gSvdLabFxStrong[playerid][e] = false;
    }
}

// Reassign attached Lab effects when the player switches their Lab target stream
static stock SvdReattachLabEffects(playerid, SV_STREAM:oldSt, SV_STREAM:newSt)
{
    for (new e = 0; e < SVD_FX_COUNT; e++)
    {
        new SV_EFFECT:fx = gSvdLabFx[playerid][e];
        if (fx != SV_NULL && SvIsValidEffect(fx))
        {
            if (oldSt != SV_NULL && SvIsValidStream(oldSt))
            {
                SvEffectDetachStream(fx, oldSt);
            }
            if (newSt != SV_NULL && SvIsValidStream(newSt))
            {
                SvEffectAttachStream(fx, newSt);
            }
        }
    }
}

// =============================================================================
// 7. Complete Cleanup Helper (Used in OnPlayerDisconnect & OnFilterScriptExit)
// =============================================================================

static stock SvdCleanupPlayerState(playerid, bool:disconnecting)
{
    SvdEndPhoneCall(
        playerid,
        disconnecting ? "{F39C12}[Phone]{FFFFFF} Call ended (peer disconnected)." : "{F39C12}[Phone]{FFFFFF} Call ended (filterscript unloaded).",
        !disconnecting
    );
    gSvdCallIncomingFrom[playerid] = INVALID_PLAYER_ID;
    gSvdCallIncomingAt[playerid] = 0;
    if (disconnecting)
    {
        for (new x = 0; x < MAX_PLAYERS; x++)
        {
            if (gSvdCallIncomingFrom[x] == playerid)
            {
                gSvdCallIncomingFrom[x] = INVALID_PLAYER_ID;
                gSvdCallIncomingAt[x] = 0;
                if (IsPlayerConnected(x))
                {
                    SendClientMessage(x, -1, "{F39C12}[Phone]{FFFFFF} The caller disconnected before you answered.");
                }
            }
        }
        gSvdAcceptCalls[playerid] = false;
        gSvdLastCallSentAt[playerid] = 0;
    }

    SvdToggleProximity(playerid, false);
    SvdJoinRadioChannel(playerid, -1);
    SvdToggleMegaphone(playerid, false);
    SvdStopIntercom(playerid);
    SvdDestroySpeakerBox(playerid);
    SvdDestroyObjSound(playerid);
    SvdDeleteAllPlayerLabEffects(playerid);

    if (IsPlayerConnected(playerid))
    {
        // Remove pair-mutes applied through this FS
        for (new s = 0; s < MAX_PLAYERS; s++)
        {
            if (SvdGetPairBit(playerid, s))
            {
                if (IsPlayerConnected(s))
                {
                    SvUnmutePlayerForListener(s, playerid);
                }
                SvdSetPairBit(playerid, s, false);
            }
            if (SvdGetPairBit(s, playerid))
            {
                if (IsPlayerConnected(s))
                {
                    SvUnmutePlayerForListener(playerid, s);
                }
                SvdSetPairBit(s, playerid, false);
            }
        }

        if (gSvdFsDisabledListener[playerid])
        {
            SvEnableListener(playerid);
            gSvdFsDisabledListener[playerid] = false;
        }
        if (gSvdFsServerMuted[playerid])
        {
            SvMutePlayerDisable(playerid);
            gSvdFsServerMuted[playerid] = false;
        }
        if (gSvdWeStartedRecord[playerid])
        {
            SvStopRecord(playerid);
            gSvdWeStartedRecord[playerid] = false;
        }
        if (gSvdOwnsKeyB[playerid]) { SvRemoveKey(playerid, SVD_KEY_LOCAL); gSvdOwnsKeyB[playerid] = false; }
        if (gSvdOwnsKeyN[playerid]) { SvRemoveKey(playerid, SVD_KEY_RADIO); gSvdOwnsKeyN[playerid] = false; }
        if (gSvdOwnsKeyM[playerid]) { SvRemoveKey(playerid, SVD_KEY_MEGA);  gSvdOwnsKeyM[playerid] = false; }
    }
}

// =============================================================================
// 8. Menus (Real-Time State UI)
// =============================================================================

static stock SvdShowMainMenu(playerid)
{
    new line[128], tmp[32];
    gSvdDlg[0] = '\0';

    // 1. Status
    new lat = SvGetPlayerVoiceLatency(playerid);
    if (lat >= 0) format(tmp, sizeof(tmp), "%d ms", lat); else format(tmp, sizeof(tmp), "n/a");
    format(line, sizeof(line), "1. My voice status {95A5A6}(latency: %s){FFFFFF}\n", tmp);
    strcat(gSvdDlg, line, sizeof(gSvdDlg));

    // 2. Proximity
    if (gSvdProxStream[playerid] != SV_NULL)
        format(line, sizeof(line), "2. Proximity chat: {2ECC71}ON{FFFFFF} (%s, %.0f m)\n", gSvdProxSlMode[playerid] ? "SL" : "DL", gSvdProxDist[playerid]);
    else
        format(line, sizeof(line), "2. Proximity chat: {E74C3C}OFF{FFFFFF}\n");
    strcat(gSvdDlg, line, sizeof(gSvdDlg));

    // 3. Radio
    if (gSvdRadioChan[playerid] >= 0)
        format(line, sizeof(line), "3. Radio channels: {2ECC71}Ch %d{FFFFFF} (N)\n", gSvdRadioChan[playerid] + 1);
    else
        format(line, sizeof(line), "3. Radio channels: {E74C3C}None{FFFFFF}\n");
    strcat(gSvdDlg, line, sizeof(gSvdDlg));

    // 4. Phone
    if (gSvdCallPeer[playerid] != INVALID_PLAYER_ID)
    {
        SvdGetSafeName(gSvdCallPeer[playerid], tmp, sizeof(tmp));
        format(line, sizeof(line), "4. Phone call: {2ECC71}In call with %s{FFFFFF}\n", tmp);
    }
    else
        format(line, sizeof(line), "4. Phone call: {95A5A6}Idle{FFFFFF}\n");
    strcat(gSvdDlg, line, sizeof(gSvdDlg));

    // 5. Megaphone
    format(line, sizeof(line), "5. Megaphone: %s{FFFFFF} (M)\n", (gSvdMegaStream[playerid] != SV_NULL) ? "{2ECC71}ON" : "{E74C3C}OFF");
    strcat(gSvdDlg, line, sizeof(gSvdDlg));

    // 6. Vehicle Intercom
    if (gSvdIntercomEnabled[playerid])
        format(line, sizeof(line), "6. Vehicle intercom: {2ECC71}ON{FFFFFF} (veh %d, %s)\n", gSvdIntercomVehId[playerid], gSvdIntercomDlMode[playerid] ? "DL" : "SL");
    else
        format(line, sizeof(line), "6. Vehicle intercom: {E74C3C}OFF{FFFFFF}\n");
    strcat(gSvdDlg, line, sizeof(gSvdDlg));

    // 7. Speaker Box
    format(line, sizeof(line), "7. Speaker box: %s{FFFFFF}\n", (gSvdBoxStream[playerid] != SV_NULL) ? "{2ECC71}PLACED" : "{E74C3C}OFF");
    strcat(gSvdDlg, line, sizeof(gSvdDlg));

    // 8. Object Sound
    format(line, sizeof(line), "8. Object sound: %s{FFFFFF}\n", (gSvdObjStream[playerid] != SV_NULL) ? "{2ECC71}ACTIVE" : "{E74C3C}OFF");
    strcat(gSvdDlg, line, sizeof(gSvdDlg));

    // 9. Parameters Lab & 10. Effects Lab
    strcat(gSvdDlg, "9. Parameters lab {95A5A6}(Volume, Pan, Pitch, Slides){FFFFFF}\n", sizeof(gSvdDlg));
    strcat(gSvdDlg, "10. Effects lab {95A5A6}(All 9 DX8 filters){FFFFFF}\n", sizeof(gSvdDlg));

    // 11. Privacy & Moderation
    format(line, sizeof(line), "11. Privacy & moderation {95A5A6}(Listening: %s){FFFFFF}\n", SvCheckListener(playerid) ? "{2ECC71}ON" : "{E74C3C}OFF");
    strcat(gSvdDlg, line, sizeof(gSvdDlg));

    // 12, 13, 14, 15
    strcat(gSvdDlg, "12. Stream explorer\n", sizeof(gSvdDlg));
    format(line, sizeof(line), "13. Live events: %s{FFFFFF}\n", gSvdFeedSelf[playerid] ? "{2ECC71}ON" : "{E74C3C}OFF");
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    strcat(gSvdDlg, "14. About SampVoice Plus\n", sizeof(gSvdDlg));
    strcat(gSvdDlg, "15. Server stats {95A5A6}(RCON Admin){FFFFFF}", sizeof(gSvdDlg));

    SvdShowDlg(playerid, SVD_DLG_MAIN, DIALOG_STYLE_LIST, "{FFE08A}SampVoice Plus - Feature Demo", gSvdDlg, "Select", "Close");
}

static stock SvdCountPlayerKeyBindings(playerid, outDesc[], size)
{
    new count = 0;
    new SV_STREAM:streams[9];
    new names[9][24];
    new s = 0;

    streams[s] = gSvdProxStream[playerid];     format(names[s], 24, "Proximity");  s++;
    streams[s] = gSvdMegaStream[playerid];     format(names[s], 24, "Megaphone");  s++;
    streams[s] = gSvdCallStream[playerid];     format(names[s], 24, "Phone");      s++;
    streams[s] = gSvdIntercomStream[playerid]; format(names[s], 24, "Intercom");   s++;
    streams[s] = gSvdBoxStream[playerid];      format(names[s], 24, "SpeakerBox"); s++;
    streams[s] = gSvdObjStream[playerid];      format(names[s], 24, "ObjSound");   s++;
    for (new r = 0; r < SVD_MAX_RADIO_CHANNELS; r++)
    {
        streams[s] = gSvdRadioStream[r];
        format(names[s], 24, "Radio %d", r + 1);
        s++;
    }

    static const sKeys[3] = { SVD_KEY_LOCAL, SVD_KEY_RADIO, SVD_KEY_MEGA };
    static const sKeyLabels[3][4] = { "B", "N", "M" };
    new item[48];
    outDesc[0] = '\0';
    for (new i = 0; i < s; i++)
    {
        if (streams[i] == SV_NULL || !SvIsValidStream(streams[i])) continue;
        for (new k = 0; k < 3; k++)
        {
            if (SvIsKeyBoundToStream(playerid, sKeys[k], streams[i]))
            {
                format(item, sizeof(item), "%s%s->%s", (count > 0) ? ", " : "", sKeyLabels[k], names[i]);
                strcat(outDesc, item, size);
                count++;
            }
        }
    }
    if (count == 0)
    {
        format(outDesc, size, "None (GM-owned or inactive)");
    }
    return count;
}

static stock SvdShowStatusMenu(playerid)
{
    new line[160], plusVerStr[24], sName[48], sType[20], bindSummary[128];
    new ver = SvGetVersion(playerid);
    new proto = SvGetClientProtocol(playerid);
    new plusVer = SvGetPlusClientVersion(playerid);
    new caps = SvGetPlusCapabilities(playerid);
    new status = SvGetPlayerVoiceStatus(playerid);
    new lat = SvGetPlayerVoiceLatency(playerid);
    SvdFormatPlusVer(plusVer, plusVerStr, sizeof(plusVerStr));

    gSvdDlg[0] = '\0';
    format(line, sizeof(line), "{FFE08A}Client:{FFFFFF} wire v%d | proto %s | Plus v%s",
           ver, (proto >= SV_PLUS_VERSION) ? "v12 (Plus)" : ((ver > 0) ? "v11 (Legacy)" : "none"), plusVerStr);
    if (proto >= SV_PLUS_VERSION && plusVer > 0 && plusVer < SV_PLUS_VERSION_NUMBER)
    {
        strcat(line, " {F39C12}[OUTDATED]", sizeof(line));
    }
    strcat(line, "\n", sizeof(line));
    strcat(gSvdDlg, line, sizeof(gSvdDlg));

    format(line, sizeof(line), "{FFE08A}Capabilities mask:{FFFFFF} 0x%X (Jitter:%s Telemetry:%s Mobile:%s)\n",
           caps, (caps & SV_CAP_ADAPTIVE_JITTER) ? "Yes" : "No", (caps & SV_CAP_TELEMETRY) ? "Yes" : "No",
           (caps & SV_CAP_MOBILE) ? "Yes" : "No");
    strcat(gSvdDlg, line, sizeof(gSvdDlg));

    new stLabel[24];
    switch (status)
    {
        case SV_VOICE_STATUS_OK:          format(stLabel, sizeof(stLabel), "{2ECC71}OK");
        case SV_VOICE_STATUS_NO_MICRO:    format(stLabel, sizeof(stLabel), "{F39C12}NO_MICRO");
        case SV_VOICE_STATUS_CANNOT_HEAR: format(stLabel, sizeof(stLabel), "{E74C3C}CANNOT_HEAR");
        default:                          format(stLabel, sizeof(stLabel), "UNKNOWN (%d)", status);
    }
    if (lat >= 0) format(plusVerStr, sizeof(plusVerStr), "%d ms", lat);
    else          format(plusVerStr, sizeof(plusVerStr), "n/a");

    format(line, sizeof(line), "{FFE08A}Voice:{FFFFFF} Mic:%s | Ready:%s | Speaking:%s | Status:%s{FFFFFF} | Latency:%s\n",
           SvHasMicro(playerid) ? "Yes" : "No",
           SvIsPlayerVoiceReady(playerid) ? "Yes" : "No",
           SvIsPlayerSpeaking(playerid) ? "Yes" : "No",
           stLabel, plusVerStr);
    strcat(gSvdDlg, line, sizeof(gSvdDlg));

    new rem = SvMutePlayerRemaining(playerid);
    if (rem > 0) format(plusVerStr, sizeof(plusVerStr), "%d ms", rem);
    else if (rem == 0) format(plusVerStr, sizeof(plusVerStr), "Indefinite");
    else format(plusVerStr, sizeof(plusVerStr), "Not muted");

    format(line, sizeof(line), "{FFE08A}Mute & Listen:{FFFFFF} ServerMuted:%s (%s) | ListenerEnabled:%s\n",
           SvMutePlayerStatus(playerid) ? "Yes" : "No", plusVerStr, SvCheckListener(playerid) ? "Yes" : "No");
    strcat(gSvdDlg, line, sizeof(gSvdDlg));

    new bindCnt = SvdCountPlayerKeyBindings(playerid, bindSummary, sizeof(bindSummary));
    format(line, sizeof(line), "{FFE08A}Registered Keys:{FFFFFF} B(0x42):%s | N(0x4E):%s | M(0x4D):%s\n",
           SvHasKey(playerid, SVD_KEY_LOCAL) ? "Yes" : "No",
           SvHasKey(playerid, SVD_KEY_RADIO) ? "Yes" : "No",
           SvHasKey(playerid, SVD_KEY_MEGA)  ? "Yes" : "No");
    strcat(gSvdDlg, line, sizeof(gSvdDlg));

    format(line, sizeof(line), "{FFE08A}Active key bindings (%d):{FFFFFF} %s\n", bindCnt, bindSummary);
    strcat(gSvdDlg, line, sizeof(gSvdDlg));

    new pstats[SV_PSTAT_COUNT];
    if (SvGetPlayerStats(playerid, pstats) > 0)
    {
        if (pstats[SV_PSTAT_LAST_VOICE_MS] >= 0) format(plusVerStr, sizeof(plusVerStr), "%d ms ago", pstats[SV_PSTAT_LAST_VOICE_MS]);
        else format(plusVerStr, sizeof(plusVerStr), "never");
        format(line, sizeof(line), "{FFE08A}Stats:{FFFFFF} RX:%d | TX:%d | FloodDrops(V:%d C:%d) | LastVoice:%s\n\n",
               pstats[SV_PSTAT_RX_VOICE_PACKETS], pstats[SV_PSTAT_TX_VOICE_PACKETS],
               pstats[SV_PSTAT_DROP_VOICE_FLOOD], pstats[SV_PSTAT_DROP_CONTROL_FLOOD], plusVerStr);
        strcat(gSvdDlg, line, sizeof(gSvdDlg));
    }

    new lCount = SvGetPlayerListenerStreams(playerid, gSvdTmpStreams, sizeof(gSvdTmpStreams));
    format(line, sizeof(line), "{FFE08A}Listening streams (%d):{FFFFFF}\n", lCount);
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    for (new i = 0; i < lCount && i < 6; i++)
    {
        if (!SvStreamGetName(gSvdTmpStreams[i], sName, sizeof(sName)) || sName[0] == '\0') format(sName, sizeof(sName), "Unnamed");
        SvdGetStreamTypeLabel(gSvdTmpStreams[i], sType, sizeof(sType));
        format(line, sizeof(line), "  - %s [%s]\n", sName, sType);
        strcat(gSvdDlg, line, sizeof(gSvdDlg));
    }

    new sCount = SvGetPlayerSpeakerStreams(playerid, gSvdTmpStreams, sizeof(gSvdTmpStreams));
    format(line, sizeof(line), "{FFE08A}Speaking streams (%d):{FFFFFF}\n", sCount);
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    for (new i = 0; i < sCount && i < 6; i++)
    {
        if (!SvStreamGetName(gSvdTmpStreams[i], sName, sizeof(sName)) || sName[0] == '\0') format(sName, sizeof(sName), "Unnamed");
        SvdGetStreamTypeLabel(gSvdTmpStreams[i], sType, sizeof(sType));
        format(line, sizeof(line), "  - %s [%s]\n", sName, sType);
        strcat(gSvdDlg, line, sizeof(gSvdDlg));
    }

    SvdShowDlg(playerid, SVD_DLG_STATUS, DIALOG_STYLE_MSGBOX, "{FFE08A}My Voice Status", gSvdDlg, "Refresh", "Back");
}

static stock SvdShowProximityMenu(playerid)
{
    new bool:on = (gSvdProxStream[playerid] != SV_NULL && SvIsValidStream(gSvdProxStream[playerid]));
    new Float:curDist = on ? SvGetStreamDistance(gSvdProxStream[playerid]) : gSvdProxDist[playerid];
    new maxP = on ? SvGetStreamMaxPlayers(gSvdProxStream[playerid]) : 24;

    gSvdDlg[0] = '\0';
    new line[128];
    format(line, sizeof(line), "Toggle proximity chat: %s{FFFFFF} (dist %.0f m, maxplayers %d, hold B)\n", on ? "{2ECC71}ON" : "{E74C3C}OFF", curDist, maxP);
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    strcat(gSvdDlg, "Range: Short (15 m)\nRange: Normal (35 m)\nRange: Wide (60 m)\n", sizeof(gSvdDlg));
    strcat(gSvdDlg, "Cycle color (White / Green / Blue / Orange)\n", sizeof(gSvdDlg));
    strcat(gSvdDlg, "Cycle name (Proximity / Nearby Chat / Whisper Zone)\n", sizeof(gSvdDlg));
    strcat(gSvdDlg, "Cycle icon (speaker / global / admin)\n", sizeof(gSvdDlg));
    format(line, sizeof(line), "Routing mode: %s\n", gSvdProxSlMode[playerid] ? "SL: the script chose the listeners (everyone online when enabled)" : "DL_PLAYER (auto distance)");
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    strcat(gSvdDlg, "< Back", sizeof(gSvdDlg));

    SvdShowDlg(playerid, SVD_DLG_PROXIMITY, DIALOG_STYLE_LIST, "{FFE08A}Proximity Chat", gSvdDlg, "Select", "Back");
}

static stock SvdShowRadioListMenu(playerid)
{
    gSvdDlg[0] = '\0';
    new line[128];
    static const sChanNames[SVD_MAX_RADIO_CHANNELS][24] = { "Alpha Radio", "Bravo Dispatch", "Charlie Squad" };
    for (new i = 0; i < SVD_MAX_RADIO_CHANNELS; i++)
    {
        new lCnt = 0, sCnt = 0, prio = 0;
        if (gSvdRadioStream[i] != SV_NULL && SvIsValidStream(gSvdRadioStream[i]))
        {
            lCnt = SvGetStreamListenerCount(gSvdRadioStream[i]);
            sCnt = SvGetStreamSpeakerCount(gSvdRadioStream[i]);
            prio = SvGetStreamPriority(gSvdRadioStream[i]);
            if (prio < 0) prio = 0;
        }
        format(line, sizeof(line), "Channel %d: %s %s{FFFFFF} (L:%d S:%d Prio:%d)\n",
               i + 1, sChanNames[i], (gSvdRadioChan[playerid] == i) ? "{2ECC71}[JOINED]" : "", lCnt, sCnt, prio);
        strcat(gSvdDlg, line, sizeof(gSvdDlg));
    }
    strcat(gSvdDlg, "Leave radio channel\n< Back", sizeof(gSvdDlg));
    SvdShowDlg(playerid, SVD_DLG_RADIO_LIST, DIALOG_STYLE_LIST, "{FFE08A}Radio Channels (Key N)", gSvdDlg, "Select", "Back");
}

static stock SvdShowRadioDetailMenu(playerid)
{
    new ch = gSvdActiveRadioUi[playerid];
    SvdEnsureRadioChannel(ch);
    new SV_GSTREAM:st = gSvdRadioStream[ch];

    new Float:myVol = SvStreamGetListenerVolume(st, playerid);
    if (myVol < 0.0) myVol = 1.0;
    new prio = SvGetStreamPriority(st);
    if (prio < 0) prio = 0;

    static const sNoiseNames[3][12] = { "Low", "Medium", "High" };
    new line[144];
    gSvdDlg[0] = '\0';
    format(line, sizeof(line), "%s (Hold N to talk)\n", (gSvdRadioChan[playerid] == ch) ? "Leave this channel" : "Join this channel");
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    format(line, sizeof(line), "Walkie-talkie filter: %s{FFFFFF} (Parameq + Distortion)\n", gSvdRadioFxOn[ch] ? "{2ECC71}ON" : "{E74C3C}OFF");
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    format(line, sizeof(line), "Radio noise preset (affects all listeners): {FFE08A}%s\n", sNoiseNames[gSvdRadioNoiseLevel[ch] % 3]);
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    format(line, sizeof(line), "My volume on this channel (cur %.0f%%): 50%%\nMy volume: 100%%\nMy volume: 150%%\n", myVol * 100.0);
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    format(line, sizeof(line), "Channel priority: {FFE08A}%d{FFFFFF} (cycle 0 / 1 / 2; ducks lower channels)\n", prio);
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    strcat(gSvdDlg, "Active key bindings (view bound keys/streams)\n< Back", sizeof(gSvdDlg));

    new title[48];
    format(title, sizeof(title), "{FFE08A}Radio Channel %d", ch + 1);
    SvdShowDlg(playerid, SVD_DLG_RADIO_DETAIL, DIALOG_STYLE_LIST, title, gSvdDlg, "Select", "Back");
}

static stock SvdShowPhoneMenu(playerid)
{
    gSvdDlg[0] = '\0';
    new line[160], pName[24];
    if (gSvdCallPeer[playerid] != INVALID_PLAYER_ID)
    {
        SvdGetSafeName(gSvdCallPeer[playerid], pName, sizeof(pName));
        format(line, sizeof(line), "{2ECC71}Active call with %s{FFFFFF} (SvStartRecord ON, N also talks)\nHang up call\n", pName);
    }
    else
    {
        format(line, sizeof(line), "Call a player (enter player ID)\n{95A5A6}No active call{FFFFFF}\n");
    }
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    format(line, sizeof(line), "Accept demo calls: %s\n< Back", gSvdAcceptCalls[playerid] ? "{2ECC71}ON" : "{E74C3C}OFF");
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    SvdShowDlg(playerid, SVD_DLG_PHONE, DIALOG_STYLE_LIST, "{FFE08A}Phone Call", gSvdDlg, "Select", "Back");
}

static stock SvdShowMegaphoneMenu(playerid)
{
    new bool:on = (gSvdMegaStream[playerid] != SV_NULL && SvIsValidStream(gSvdMegaStream[playerid]));
    format(gSvdDlg, sizeof(gSvdDlg), "Toggle megaphone: %s{FFFFFF} (95 m range + Distortion, hold M)\n< Back", on ? "{2ECC71}ON" : "{E74C3C}OFF");
    SvdShowDlg(playerid, SVD_DLG_MEGAPHONE, DIALOG_STYLE_LIST, "{FFE08A}Megaphone", gSvdDlg, "Select", "Back");
}

static stock SvdShowIntercomMenu(playerid)
{
    new vehId = GetPlayerVehicleID(playerid);
    new line[192];
    gSvdDlg[0] = '\0';
    if (vehId <= 0)
    {
        format(line, sizeof(line), "{E74C3C}Not inside a vehicle.{FFFFFF}\nMode: %s\n< Back", gSvdIntercomDlMode[playerid] ? "DL_VEHICLE" : "SL_VEHICLE (occupants only)");
    }
    else
    {
        format(line, sizeof(line), "Toggle vehicle intercom: %s{FFFFFF} (veh %d, hold B)\nMode: %s (Only occupants hear SL; script picks listeners)\n< Back",
               gSvdIntercomEnabled[playerid] ? "{2ECC71}ON" : "{E74C3C}OFF", vehId,
               gSvdIntercomDlMode[playerid] ? "DL_VEHICLE (nearby outside also hear)" : "SL_VEHICLE (occupants only)");
    }
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    SvdShowDlg(playerid, SVD_DLG_INTERCOM, DIALOG_STYLE_LIST, "{FFE08A}Vehicle Intercom", gSvdDlg, "Select", "Back");
}

static stock SvdShowSpeakerBoxMenu(playerid)
{
    new SV_LPSTREAM:st = gSvdBoxStream[playerid];
    new bool:on = (st != SV_NULL && SvIsValidStream(st));
    gSvdDlg[0] = '\0';
    new line[160];
    format(line, sizeof(line), "Place speaker box here %s\n", on ? "{95A5A6}(replaces current){FFFFFF}" : "");
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    strcat(gSvdDlg, "Move here (SvUpdatePositionForLPStream)\n", sizeof(gSvdDlg));
    format(line, sizeof(line), "World/interior filter: %s\n", gSvdBoxWorldFiltered[playerid] ? "{2ECC71}My VW & Interior" : "{FFE08A}Any (-1)");
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    strcat(gSvdDlg, gSvdBoxSlMode[playerid] ? "Kick everyone (SL: detaches listeners & speakers)\n" : "Kick everyone (DL: nearby listeners auto-reattach in ~30 ms)\n", sizeof(gSvdDlg));
    strcat(gSvdDlg, "Inspect box stream (Type, Pos, Listeners, SvIsValidStream)\n", sizeof(gSvdDlg));
    format(line, sizeof(line), "Mode: %s\n", gSvdBoxSlMode[playerid] ? "SL: the script chose the listeners (everyone online when placed)" : "DL_POINT (auto-range)");
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    strcat(gSvdDlg, "Remove speaker box\n< Back", sizeof(gSvdDlg));
    SvdShowDlg(playerid, SVD_DLG_SPEAKERBOX, DIALOG_STYLE_LIST, "{FFE08A}Speaker Box (Hold B)", gSvdDlg, "Select", "Back");
}

static stock SvdShowObjSoundMenu(playerid)
{
    new bool:on = (gSvdObjStream[playerid] != SV_NULL && SvIsValidStream(gSvdObjStream[playerid]));
    new line[192], sTargetStr[24];
    if (on) format(sTargetStr, sizeof(sTargetStr), "%d", SvGetStreamTarget(gSvdObjStream[playerid]));
    else    format(sTargetStr, sizeof(sTargetStr), "-1");

    gSvdDlg[0] = '\0';
    format(line, sizeof(line), "Spawn speaker object + audio here (%s, hold B)\n", on ? "{2ECC71}ACTIVE" : "{E74C3C}OFF");
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    format(line, sizeof(line), "Move object back & forth (target objectid: %s)\n", sTargetStr);
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    format(line, sizeof(line), "Mode: %s\n", gSvdObjSlMode[playerid] ? "SL: the script chose the listeners (everyone online when placed)" : "DL_OBJECT (auto-range)");
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    strcat(gSvdDlg, "Remove object & stream\nNote: Standard CreateObject only (use point stream for Streamer objects)\n< Back", sizeof(gSvdDlg));
    SvdShowDlg(playerid, SVD_DLG_OBJSOUND, DIALOG_STYLE_LIST, "{FFE08A}Object Sound", gSvdDlg, "Select", "Back");
}

static stock SvdShowParamsMenu(playerid)
{
    new tLbl[80], line[128];
    SvdGetLabTargetLabel(playerid, tLbl, sizeof(tLbl));
    new SV_STREAM:st = SvdResolveLabStream(playerid);
    new Float:v = 1.0, Float:pan = 0.0, Float:freq = 0.0;
    new bool:hasV = false, bool:hasP = false, bool:hasF = false;
    if (st != SV_NULL && SvIsValidStream(st))
    {
        SvStreamParameterGetEx(st, SV_PARAMETER_VOLUME, v);
        SvStreamParameterGetEx(st, SV_PARAMETER_PANNING, pan);
        SvStreamParameterGetEx(st, SV_PARAMETER_FREQUENCY, freq);
        hasV = SvStreamParameterHas(st, SV_PARAMETER_VOLUME);
        hasP = SvStreamParameterHas(st, SV_PARAMETER_PANNING);
        hasF = SvStreamParameterHas(st, SV_PARAMETER_FREQUENCY);
    }

    gSvdDlg[0] = '\0';
    format(line, sizeof(line), "Target: {FFE08A}%s\n", tLbl);
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    format(line, sizeof(line), "Volume 50%% / 100%% / 150%% (cur %.2f, set:%s)\n", v, hasV ? "Y" : "N");
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    strcat(gSvdDlg, "Volume smooth fade-in (SlideFromTo 0.1 -> 1.0 in 800 ms)\n", sizeof(gSvdDlg));
    strcat(gSvdDlg, "Volume fade-out (SlideTo -> 0.15 in 800 ms)\n", sizeof(gSvdDlg));
    strcat(gSvdDlg, "Volume step up (Slide delta +0.25 in 400 ms)\n", sizeof(gSvdDlg));
    format(line, sizeof(line), "Pan Left (-0.8) / Center (0.0) / Right (+0.8) (cur %.2f, set:%s)\n", pan, hasP ? "Y" : "N");
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    format(line, sizeof(line), "Pitch 36000 Hz / 60000 Hz / Original 0.0 Hz (cur %.0f, set:%s)\n", freq, hasF ? "Y" : "N");
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    strcat(gSvdDlg, "Reset all parameters (Vol, Pan, Frequency)\n< Back", sizeof(gSvdDlg));
    SvdShowDlg(playerid, SVD_DLG_PARAMS, DIALOG_STYLE_LIST, "{FFE08A}Parameters Lab", gSvdDlg, "Select", "Back");
}

static stock SvdShowEffectsMenu(playerid)
{
    new tLbl[80], line[128], fxName[24];
    SvdGetLabTargetLabel(playerid, tLbl, sizeof(tLbl));
    gSvdDlg[0] = '\0';
    format(line, sizeof(line), "Target: {FFE08A}%s\n", tLbl);
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    for (new e = 0; e < SVD_FX_COUNT; e++)
    {
        SvdGetEffectLabel(e, fxName, sizeof(fxName));
        new bool:active = (gSvdLabFx[playerid][e] != SV_NULL && SvIsValidEffect(gSvdLabFx[playerid][e]));
        format(line, sizeof(line), "%d. %s: %s{FFFFFF}%s\n", e + 1, fxName,
               active ? "{2ECC71}ON" : "{E74C3C}OFF",
               active ? (gSvdLabFxStrong[playerid][e] ? " (Strong)" : " (Soft)") : "");
        strcat(gSvdDlg, line, sizeof(gSvdDlg));
    }
    strcat(gSvdDlg, "< Back", sizeof(gSvdDlg));
    SvdShowDlg(playerid, SVD_DLG_EFFECTS, DIALOG_STYLE_LIST, "{FFE08A}Effects Lab (All 9 DX8 Filters)", gSvdDlg, "Select", "Back");
}

static stock SvdShowEffectItemMenu(playerid)
{
    new e = gSvdSelectedLabFx[playerid];
    new fxName[24], line[128];
    SvdGetEffectLabel(e, fxName, sizeof(fxName));
    new bool:active = (gSvdLabFx[playerid][e] != SV_NULL && SvIsValidEffect(gSvdLabFx[playerid][e]));

    gSvdDlg[0] = '\0';
    format(line, sizeof(line), "%s (SvIsValidEffect: %s)\n", active ? "Disable & delete effect" : "Enable & attach effect", active ? "true" : "false");
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    format(line, sizeof(line), "Preset: %s (live update via SvEffectUpdate*)\n", gSvdLabFxStrong[playerid][e] ? "Strong -> Switch to Softer" : "Soft -> Switch to Stronger");
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    strcat(gSvdDlg, "< Back", sizeof(gSvdDlg));

    new title[48];
    format(title, sizeof(title), "{FFE08A}DX8 Effect: %s", fxName);
    SvdShowDlg(playerid, SVD_DLG_EFFECTS_ITEM, DIALOG_STYLE_LIST, title, gSvdDlg, "Select", "Back");
}

static stock SvdShowLabStreamPicker(playerid, dialogId)
{
    gSvdDlg[0] = '\0';
    static const sLabels[9][28] = {
        "My Proximity stream", "My Megaphone stream", "My Speaker Box stream",
        "My Object Sound stream", "My Vehicle Intercom", "Radio Channel 1",
        "Radio Channel 2", "Radio Channel 3", "Active Phone Call"
    };
    new line[80];
    for (new s = 0; s < 9; s++)
    {
        new SV_STREAM:st = SvdResolveLabStream(playerid, s);
        new bool:valid = (st != SV_NULL && SvIsValidStream(st));
        format(line, sizeof(line), "%s %s\n", sLabels[s], valid ? "{2ECC71}[Ready]" : "{95A5A6}[Off]");
        strcat(gSvdDlg, line, sizeof(gSvdDlg));
    }
    strcat(gSvdDlg, "< Back", sizeof(gSvdDlg));
    SvdShowDlg(playerid, dialogId, DIALOG_STYLE_LIST, "{FFE08A}Pick Target Stream", gSvdDlg, "Select", "Back");
}

static stock SvdShowPrivacyMenu(playerid)
{
    gSvdDlg[0] = '\0';
    new line[144];
    strcat(gSvdDlg, "Mute / unmute a player FOR ME ONLY (server-side pair mute)\n", sizeof(gSvdDlg));
    format(line, sizeof(line), "Stop hearing everyone: %s{FFFFFF} (F11 shows 'disabled by server')\n",
           SvCheckListener(playerid) ? "{2ECC71}Hearing OK" : "{E74C3C}Stopped");
    strcat(gSvdDlg, line, sizeof(gSvdDlg));

    if (IsPlayerAdmin(playerid))
    {
        strcat(gSvdDlg, "{FFE08A}[RCON Admin]{FFFFFF} Server-mute player (30 s, releasekeys=true) / Unmute\n", sizeof(gSvdDlg));
        format(line, sizeof(line), "{FFE08A}[RCON Admin]{FFFFFF} Server debug logging (SvDebug): %s\n", gSvdFsToggledDebug ? "{2ECC71}ON" : "{95A5A6}OFF");
        strcat(gSvdDlg, line, sizeof(gSvdDlg));
    }
    strcat(gSvdDlg, "< Back", sizeof(gSvdDlg));
    SvdShowDlg(playerid, SVD_DLG_PRIVACY, DIALOG_STYLE_LIST, "{FFE08A}Privacy & Moderation", gSvdDlg, "Select", "Back");
}

static stock SvdShowExplorerList(playerid)
{
    gSvdDlg[0] = '\0';
    new line[128], sName[40], sType[20];
    static const sLabels[9][20] = { "Proximity", "Megaphone", "SpeakerBox", "ObjSound", "VehIntercom", "Radio1", "Radio2", "Radio3", "PhoneCall" };
    for (new s = 0; s < 9; s++)
    {
        new SV_STREAM:st = SvdResolveLabStream(playerid, s);
        if (st != SV_NULL && SvIsValidStream(st))
        {
            if (!SvStreamGetName(st, sName, sizeof(sName)) || sName[0] == '\0') format(sName, sizeof(sName), "Unnamed");
            SvdGetStreamTypeLabel(st, sType, sizeof(sType));
            format(line, sizeof(line), "%s [%s] {95A5A6}(0x%08x){FFFFFF} L:%d S:%d\n",
                   sName, sType, SvStreamGetColor(st), SvGetStreamListenerCount(st), SvGetStreamSpeakerCount(st));
        }
        else
        {
            format(line, sizeof(line), "{95A5A6}%s (not active){FFFFFF}\n", sLabels[s]);
        }
        strcat(gSvdDlg, line, sizeof(gSvdDlg));
    }
    strcat(gSvdDlg, "< Back", sizeof(gSvdDlg));
    SvdShowDlg(playerid, SVD_DLG_EXPLORER, DIALOG_STYLE_LIST, "{FFE08A}Stream Explorer", gSvdDlg, "Inspect", "Back");
}

static stock SvdShowExplorerDetail(playerid, slot)
{
    new SV_STREAM:st = SvdResolveLabStream(playerid, slot);
    if (st == SV_NULL || !SvIsValidStream(st))
    {
        SendClientMessage(playerid, -1, "{F39C12}[Explorer]{FFFFFF} That demo stream is currently off.");
        SvdShowExplorerList(playerid);
        return;
    }

    new sName[64], sType[20], line[160], ids[12], pName[24];
    if (!SvStreamGetName(st, sName, sizeof(sName)) || sName[0] == '\0') format(sName, sizeof(sName), "Unnamed");
    SvdGetStreamTypeLabel(st, sType, sizeof(sType));
    new Float:px = 0.0, Float:py = 0.0, Float:pz = 0.0;
    new bool:hasPos = SvGetStreamPosition(st, px, py, pz);

    gSvdDlg[0] = '\0';
    format(line, sizeof(line), "{FFE08A}Name:{FFFFFF} %s | {FFE08A}Type:{FFFFFF} %s | {FFE08A}RawColor:{FFFFFF} 0x%08x\n",
           sName, sType, SvStreamGetColor(st));
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    format(line, sizeof(line), "{FFE08A}Target:{FFFFFF} %d | {FFE08A}Dist:{FFFFFF} %.1f m | {FFE08A}MaxPlayers:{FFFFFF} %d\n",
           SvGetStreamTarget(st), SvGetStreamDistance(st), SvGetStreamMaxPlayers(st));
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    format(line, sizeof(line), "{FFE08A}VW/Int:{FFFFFF} %d / %d | {FFE08A}Pos:{FFFFFF} %s\n\n",
           SvGetStreamWorld(st), SvGetStreamInterior(st), hasPos ? "Point" : "N/A");
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    if (hasPos)
    {
        format(line, sizeof(line), "{FFE08A}Coords:{FFFFFF} (%.2f, %.2f, %.2f)\n\n", px, py, pz);
        strcat(gSvdDlg, line, sizeof(gSvdDlg));
    }

    new lCnt = SvGetStreamListeners(st, ids, sizeof(ids));
    format(line, sizeof(line), "{FFE08A}Listeners (%d):{FFFFFF} ", SvGetStreamListenerCount(st));
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    for (new i = 0; i < lCnt; i++)
    {
        SvdGetSafeName(ids[i], pName, sizeof(pName));
        format(line, sizeof(line), "%s%s(%d)", (i > 0) ? ", " : "", pName, ids[i]);
        strcat(gSvdDlg, line, sizeof(gSvdDlg));
    }
    if (lCnt == 0) strcat(gSvdDlg, "None", sizeof(gSvdDlg));

    new sCnt = SvGetStreamSpeakers(st, ids, sizeof(ids));
    format(line, sizeof(line), "\n{FFE08A}Speakers (%d):{FFFFFF} ", SvGetStreamSpeakerCount(st));
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    for (new i = 0; i < sCnt; i++)
    {
        SvdGetSafeName(ids[i], pName, sizeof(pName));
        format(line, sizeof(line), "%s%s(%d)", (i > 0) ? ", " : "", pName, ids[i]);
        strcat(gSvdDlg, line, sizeof(gSvdDlg));
    }
    if (sCnt == 0) strcat(gSvdDlg, "None", sizeof(gSvdDlg));

    SvdShowDlg(playerid, SVD_DLG_EXPLORER_DETAIL, DIALOG_STYLE_MSGBOX, "{FFE08A}Stream Details", gSvdDlg, "Back", "Close");
}

static stock SvdShowLiveEventsMenu(playerid)
{
    gSvdDlg[0] = '\0';
    new line[160];
    format(line, sizeof(line), "My voice events in chat: %s{FFFFFF}\n", gSvdFeedSelf[playerid] ? "{2ECC71}ON" : "{E74C3C}OFF");
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    if (IsPlayerAdmin(playerid))
    {
        format(line, sizeof(line), "{FFE08A}[RCON Admin]{FFFFFF} All players' voice events: %s{FFFFFF}\n", gSvdFeedAll[playerid] ? "{2ECC71}ON" : "{E74C3C}OFF");
        strcat(gSvdDlg, line, sizeof(gSvdDlg));
    }
    strcat(gSvdDlg, "{95A5A6}Note: OnPlayerVoiceReady only fires for players joining after FS load;{FFFFFF}\n", sizeof(gSvdDlg));
    strcat(gSvdDlg, "{95A5A6}use '1. My voice status' (SvIsPlayerVoiceReady) for already connected players.{FFFFFF}\n", sizeof(gSvdDlg));
    strcat(gSvdDlg, "< Back", sizeof(gSvdDlg));
    SvdShowDlg(playerid, SVD_DLG_LIVE_EVENTS, DIALOG_STYLE_LIST, "{FFE08A}Live Callback Feed", gSvdDlg, "Toggle", "Back");
}

static stock SvdShowAboutDialog(playerid)
{
    gSvdDlg[0] = '\0';
    new line[160];
    format(line, sizeof(line), "{FFE08A}SampVoice Plus v%s{FFFFFF} - Keys: B (Local), N (Radio/Phone), M (Megaphone)\n\n", SV_PLUS_VERSION_STRING);
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    strcat(gSvdDlg, "{FFE08A}Server config defaults (server.cfg / config.json):{FFFFFF}\n", sizeof(gSvdDlg));
    strcat(gSvdDlg, "  port=0, bind=0.0.0.0, threads=4, updaterate=30, legacy_color_format=0,\n", sizeof(gSvdDlg));
    strcat(gSvdDlg, "  voice_status=1, nearby_list=1, allow_toggle_mode=1, voice_occlusion=1, priority_duck=50\n\n", sizeof(gSvdDlg));
    strcat(gSvdDlg, "{FFE08A}Features requiring SampVoice Plus client:{FFFFFF}\n", sizeof(gSvdDlg));
    strcat(gSvdDlg, "  Icons (1.0+), occlusion & toggle enforcement (1.2+), live rename/color,\n", sizeof(gSvdDlg));
    strcat(gSvdDlg, "  in-place effect updates & latency reporting (1.3+).\n", sizeof(gSvdDlg));
    strcat(gSvdDlg, "  Note: Android 3.x launchers always send key B (0x42).\n\n", sizeof(gSvdDlg));
    strcat(gSvdDlg, "{FFE08A}GM safety:{FFFFFF} This FS never calls SvInit (server-wide bitrate) or\n", sizeof(gSvdDlg));
    strcat(gSvdDlg, "  SvRemoveAllKeys (would wipe gamemode keys).\n\n", sizeof(gSvdDlg));
    strcat(gSvdDlg, "{FFE08A}Repo:{FFFFFF} https://github.com/Walter-Correa/SampVoice-Plus", sizeof(gSvdDlg));
    SvdShowDlg(playerid, SVD_DLG_ABOUT, DIALOG_STYLE_MSGBOX, "{FFE08A}About SampVoice Plus", gSvdDlg, "Back", "Close");
}

static stock SvdShowKeyBindsDialog(playerid)
{
    new summary[256], line[160];
    new cnt = SvdCountPlayerKeyBindings(playerid, summary, sizeof(summary));
    gSvdDlg[0] = '\0';
    format(line, sizeof(line), "{FFE08A}Active key-to-stream bindings:{FFFFFF} %d\n\n", cnt);
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    format(line, sizeof(line), "%s\n\n", summary);
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    format(line, sizeof(line), "{FFE08A}Ownership:{FFFFFF} FS owns B:%s | N:%s | M:%s\n",
           gSvdOwnsKeyB[playerid] ? "Yes" : "No (GM/idle)",
           gSvdOwnsKeyN[playerid] ? "Yes (Radio N auto-bound)" : "No (GM/idle)",
           gSvdOwnsKeyM[playerid] ? "Yes" : "No (GM/idle)");
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    strcat(gSvdDlg, "{95A5A6}Keys owned by your gamemode are never modified by this FS.{FFFFFF}", sizeof(gSvdDlg));
    SvdShowDlg(playerid, SVD_DLG_KEYBINDS, DIALOG_STYLE_MSGBOX, "{FFE08A}Active Key Bindings", gSvdDlg, "Back", "Close");
}

static stock SvdShowServerStatsDialog(playerid)
{
    new st[SV_STAT_COUNT], line[160];
    if (SvGetServerStats(st) <= 0)
    {
        SendClientMessage(playerid, -1, "{E74C3C}[SV+ Demo]{FFFFFF} Server stats unavailable.");
        SvdShowMainMenu(playerid);
        return;
    }

    gSvdDlg[0] = '\0';
    format(line, sizeof(line), "{FFE08A}General:{FFFFFF} Uptime %d s | Worker threads: %d\n",
           st[SV_STAT_UPTIME], st[SV_STAT_WORKER_THREADS]);
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    format(line, sizeof(line), "{FFE08A}Players:{FFFFFF} Plugin:%d | Plus:%d (Mob:%d) | v11:%d | Ready:%d | Speaking:%d\n",
           st[SV_STAT_PLUGIN_PLAYERS], st[SV_STAT_PLUS_PLAYERS], st[SV_STAT_MOBILE_PLAYERS],
           st[SV_STAT_LEGACY_PLAYERS], st[SV_STAT_VOICE_READY_PLAYERS], st[SV_STAT_SPEAKING_PLAYERS]);
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    format(line, sizeof(line), "{FFE08A}Streams:{FFFFFF} Total:%d (G:%d SL:%d DL:%d) | Effects:%d\n",
           st[SV_STAT_STREAMS_TOTAL], st[SV_STAT_STREAMS_GLOBAL], st[SV_STAT_STREAMS_SL],
           st[SV_STAT_STREAMS_DL], st[SV_STAT_EFFECTS_TOTAL]);
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    format(line, sizeof(line), "{FFE08A}Attachments:{FFFFFF} Listeners:%d | Speakers:%d\n",
           st[SV_STAT_STREAM_LISTENERS], st[SV_STAT_STREAM_SPEAKERS]);
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    format(line, sizeof(line), "{FFE08A}Voice packets:{FFFFFF} RX:%d (%d/s, %d B/s) | TX:%d (%d/s, %d B/s)\n",
           st[SV_STAT_RX_VOICE_PACKETS], st[SV_STAT_RX_VOICE_PPS], st[SV_STAT_RX_VOICE_BPS],
           st[SV_STAT_TX_VOICE_PACKETS], st[SV_STAT_TX_VOICE_PPS], st[SV_STAT_TX_VOICE_BPS]);
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    format(line, sizeof(line), "{FFE08A}Drops/Errors:{FFFFFF} VoiceFlood:%d | CtrlFlood:%d | Invalid:%d | CtrlQDrop:%d\n",
           st[SV_STAT_DROP_VOICE_FLOOD], st[SV_STAT_DROP_CONTROL_FLOOD],
           st[SV_STAT_INVALID_PACKETS], st[SV_STAT_CONTROL_QUEUE_DROPS]);
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    format(line, sizeof(line), "{FFE08A}Other:{FFFFFF} NAT updates:%d | PendingDeletes:%d | PendingRecreates:%d\n",
           st[SV_STAT_NAT_PORT_UPDATES], st[SV_STAT_PENDING_DELETES], st[SV_STAT_PENDING_RECREATES]);
    strcat(gSvdDlg, line, sizeof(gSvdDlg));
    format(line, sizeof(line), "{FFE08A}Tick (last 60s):{FFFFFF} Avg:%d us | Max:%d us",
           st[SV_STAT_TICK_AVG_US], st[SV_STAT_TICK_MAX_US]);
    strcat(gSvdDlg, line, sizeof(gSvdDlg));

    SvdShowDlg(playerid, SVD_DLG_SERVER_STATS, DIALOG_STYLE_MSGBOX, "{FFE08A}Voice Server Stats (RCON)", gSvdDlg, "Refresh", "Back");
}

// =============================================================================
// 9. Lifecycle, Commands & Dialog Routing
// =============================================================================

public OnFilterScriptInit()
{
    for (new i = 0; i < MAX_PLAYERS; i++)
    {
        SvdInitPlayerVars(i);
    }
    for (new c = 0; c < SVD_MAX_RADIO_CHANNELS; c++)
    {
        gSvdRadioStream[c] = SV_NULL;
        gSvdRadioFxOn[c] = false;
        gSvdRadioNoiseLevel[c] = 0;
        gSvdRadioParameqFx[c] = SV_NULL;
        gSvdRadioDistFx[c] = SV_NULL;
    }
    printf("[SampVoice Plus] Demo filterscript v%s loaded (/voice or /voip).", SV_PLUS_VERSION_STRING);
    return 1;
}

public OnFilterScriptExit()
{
    for (new i = 0; i < MAX_PLAYERS; i++)
    {
        SvdCleanupPlayerState(i, false);
    }
    for (new c = 0; c < SVD_MAX_RADIO_CHANNELS; c++)
    {
        if (gSvdRadioParameqFx[c] != SV_NULL)
        {
            SvEffectDelete(gSvdRadioParameqFx[c]);
            gSvdRadioParameqFx[c] = SV_NULL;
        }
        if (gSvdRadioDistFx[c] != SV_NULL)
        {
            SvEffectDelete(gSvdRadioDistFx[c]);
            gSvdRadioDistFx[c] = SV_NULL;
        }
        if (gSvdRadioStream[c] != SV_NULL)
        {
            SvDeleteStream(gSvdRadioStream[c]);
            gSvdRadioStream[c] = SV_NULL;
        }
    }
    if (gSvdFsToggledDebug)
    {
        SvDebug(false);
        gSvdFsToggledDebug = false;
    }
    return 1;
}

public OnPlayerConnect(playerid)
{
    SvdInitPlayerVars(playerid);
    return 1;
}

public OnPlayerDisconnect(playerid, reason)
{
    SvdCleanupPlayerState(playerid, true);
    return 1;
}

public OnPlayerSpawn(playerid)
{
    if (!gSvdWelcomed[playerid])
    {
        gSvdWelcomed[playerid] = true;
        SendClientMessage(playerid, -1,
            "{2ECC71}[SampVoice Plus]{FFFFFF} Voice demo loaded. Type {FFE08A}/voice{FFFFFF} (or {FFE08A}/voip{FFFFFF}) to try every feature.");
    }
    return 1;
}

#if defined _INC_open_mp
public OnPlayerStateChange(playerid, PLAYER_STATE:newstate, PLAYER_STATE:oldstate)
#else
public OnPlayerStateChange(playerid, newstate, oldstate)
#endif
{
    new vehNow = GetPlayerVehicleID(playerid);
    // Sync passenger memberships for any active SL vehicle intercom streams
    for (new owner = 0; owner < MAX_PLAYERS; owner++)
    {
        if (!IsPlayerConnected(owner) || !gSvdIntercomEnabled[owner] || gSvdIntercomStream[owner] == SV_NULL) continue;
        if (owner == playerid && newstate != PLAYER_STATE_DRIVER && newstate != PLAYER_STATE_PASSENGER)
        {
            SvdStopIntercom(owner);
            SendClientMessage(owner, -1, "{F39C12}[Intercom]{FFFFFF} Stopped because you left or died in the vehicle.");
            continue;
        }
        if (!gSvdIntercomDlMode[owner] && owner != playerid)
        {
            if (vehNow == gSvdIntercomVehId[owner])
            {
                if (!SvHasListenerInStream(gSvdIntercomStream[owner], playerid))
                {
                    SvAttachListenerToStream(gSvdIntercomStream[owner], playerid);
                }
            }
            else if (SvHasListenerInStream(gSvdIntercomStream[owner], playerid))
            {
                SvDetachListenerFromStream(gSvdIntercomStream[owner], playerid);
            }
        }
    }
    return 1;
}

public OnPlayerCommandText(playerid, cmdtext[])
{
    if (strcmp(cmdtext, "/voice", true) == 0 || strcmp(cmdtext, "/voip", true) == 0)
    {
        if (SvGetVersion(playerid) == SV_NULL)
        {
            SendClientMessage(playerid, -1,
                "{F39C12}[SampVoice Plus]{FFFFFF} No voice plugin detected on your client; read-only exploration is still open.");
        }
        SvdShowMainMenu(playerid);
        return 1;
    }
    return 0;
}

public OnDialogResponse(playerid, dialogid, response, listitem, inputtext[])
{
    if (dialogid < SVD_DIALOG_BASE || dialogid > SVD_DLG_SERVER_STATS)
    {
        return 0;
    }

    switch (dialogid)
    {
        case SVD_DLG_MAIN:
        {
            if (!response) return 1;
            switch (listitem)
            {
                case 0:  SvdShowStatusMenu(playerid);
                case 1:  SvdShowProximityMenu(playerid);
                case 2:  SvdShowRadioListMenu(playerid);
                case 3:  SvdShowPhoneMenu(playerid);
                case 4:  SvdShowMegaphoneMenu(playerid);
                case 5:  SvdShowIntercomMenu(playerid);
                case 6:  SvdShowSpeakerBoxMenu(playerid);
                case 7:  SvdShowObjSoundMenu(playerid);
                case 8:  SvdShowParamsMenu(playerid);
                case 9:  SvdShowEffectsMenu(playerid);
                case 10: SvdShowPrivacyMenu(playerid);
                case 11: SvdShowExplorerList(playerid);
                case 12: SvdShowLiveEventsMenu(playerid);
                case 13: SvdShowAboutDialog(playerid);
                case 14:
                {
                    if (!IsPlayerAdmin(playerid))
                    {
                        SendClientMessage(playerid, -1, "{E74C3C}[SV+ Demo]{FFFFFF} RCON admin login required to view server stats.");
                        SvdShowMainMenu(playerid);
                    }
                    else
                    {
                        SvdShowServerStatsDialog(playerid);
                    }
                }
            }
            return 1;
        }

        case SVD_DLG_STATUS:
        {
            if (response) SvdShowStatusMenu(playerid); else SvdShowMainMenu(playerid);
            return 1;
        }

        case SVD_DLG_PROXIMITY:
        {
            if (!response || listitem == 8) { SvdShowMainMenu(playerid); return 1; }
            switch (listitem)
            {
                case 0: SvdToggleProximity(playerid, gSvdProxStream[playerid] == SV_NULL);
                case 1:
                {
                    gSvdProxDist[playerid] = 15.0;
                    if (gSvdProxStream[playerid] != SV_NULL) SvUpdateDistanceForLStream(gSvdProxStream[playerid], 15.0);
                }
                case 2:
                {
                    gSvdProxDist[playerid] = 35.0;
                    if (gSvdProxStream[playerid] != SV_NULL) SvUpdateDistanceForLStream(gSvdProxStream[playerid], 35.0);
                }
                case 3:
                {
                    gSvdProxDist[playerid] = 60.0;
                    if (gSvdProxStream[playerid] != SV_NULL) SvUpdateDistanceForLStream(gSvdProxStream[playerid], 60.0);
                }
                case 4: { gSvdProxColorIdx[playerid] = (gSvdProxColorIdx[playerid] + 1) % 4; SvdApplyProxStyle(playerid); }
                case 5: { gSvdProxNameIdx[playerid]  = (gSvdProxNameIdx[playerid] + 1) % 3;  SvdApplyProxStyle(playerid); }
                case 6: { gSvdProxIconIdx[playerid]  = (gSvdProxIconIdx[playerid] + 1) % 3;  SvdApplyProxStyle(playerid); }
                case 7:
                {
                    gSvdProxSlMode[playerid] = !gSvdProxSlMode[playerid];
                    if (gSvdProxStream[playerid] != SV_NULL) SvdToggleProximity(playerid, true);
                }
            }
            SvdShowProximityMenu(playerid);
            return 1;
        }

        case SVD_DLG_RADIO_LIST:
        {
            if (!response || listitem == 4) { SvdShowMainMenu(playerid); return 1; }
            if (listitem == 3) { SvdJoinRadioChannel(playerid, -1); SvdShowRadioListMenu(playerid); return 1; }
            gSvdActiveRadioUi[playerid] = listitem;
            SvdShowRadioDetailMenu(playerid);
            return 1;
        }

        case SVD_DLG_RADIO_DETAIL:
        {
            if (!response || listitem == 8) { SvdShowRadioListMenu(playerid); return 1; }
            new ch = gSvdActiveRadioUi[playerid];
            SvdEnsureRadioChannel(ch);
            new SV_GSTREAM:st = gSvdRadioStream[ch];
            switch (listitem)
            {
                case 0: SvdJoinRadioChannel(playerid, (gSvdRadioChan[playerid] == ch) ? -1 : ch);
                case 1: SvdApplyRadioFilter(ch, !gSvdRadioFxOn[ch], gSvdRadioNoiseLevel[ch]);
                case 2:
                {
                    new nextNoise = (gSvdRadioNoiseLevel[ch] + 1) % 3;
                    SvdApplyRadioFilter(ch, true, nextNoise);
                }
                case 3: if (st != SV_NULL) SvStreamSetListenerVolume(st, playerid, 0.5);
                case 4: if (st != SV_NULL) SvStreamSetListenerVolume(st, playerid, 1.0);
                case 5: if (st != SV_NULL) SvStreamSetListenerVolume(st, playerid, 1.5);
                case 6:
                {
                    if (st != SV_NULL)
                    {
                        new curPrio = SvGetStreamPriority(st);
                        if (curPrio < 0) curPrio = 0;
                        SvSetStreamPriority(st, (curPrio + 1) % 3);
                    }
                }
                case 7:
                {
                    SvdShowKeyBindsDialog(playerid);
                    return 1;
                }
            }
            SvdShowRadioDetailMenu(playerid);
            return 1;
        }

        case SVD_DLG_PHONE:
        {
            if (!response || listitem == 3) { SvdShowMainMenu(playerid); return 1; }
            if (listitem == 2)
            {
                gSvdAcceptCalls[playerid] = !gSvdAcceptCalls[playerid];
                SvdShowPhoneMenu(playerid);
                return 1;
            }
            if (gSvdCallPeer[playerid] != INVALID_PLAYER_ID)
            {
                if (listitem == 1) SvdEndPhoneCall(playerid, "{F39C12}[Phone]{FFFFFF} Call ended.");
                SvdShowPhoneMenu(playerid);
                return 1;
            }
            if (listitem == 0)
            {
                format(gSvdDlg, sizeof(gSvdDlg), "Enter the player ID you want to call:");
                SvdShowDlg(playerid, SVD_DLG_PHONE_PICK, DIALOG_STYLE_INPUT, "{FFE08A}Call Player", gSvdDlg, "Call", "Back");
                return 1;
            }
            SvdShowPhoneMenu(playerid);
            return 1;
        }

        case SVD_DLG_PHONE_PICK:
        {
            if (!response) { SvdShowPhoneMenu(playerid); return 1; }
            if (gSvdCallPeer[playerid] != INVALID_PLAYER_ID)
            {
                SendClientMessage(playerid, -1, "{F39C12}[Phone]{FFFFFF} You are already in an active call.");
                SvdShowPhoneMenu(playerid);
                return 1;
            }
            new targetid = INVALID_PLAYER_ID;
            if (!SvdParseIntStrict(inputtext, targetid) || targetid == playerid || !IsPlayerConnected(targetid))
            {
                SendClientMessage(playerid, -1, "{E74C3C}[Phone]{FFFFFF} Invalid or offline player ID.");
                SvdShowPhoneMenu(playerid);
                return 1;
            }
            if (!gSvdAcceptCalls[targetid])
            {
                SendClientMessage(playerid, -1, "{F39C12}[Phone]{FFFFFF} That player is not accepting demo calls.");
                SvdShowPhoneMenu(playerid);
                return 1;
            }
            if (gSvdCallPeer[targetid] != INVALID_PLAYER_ID)
            {
                SendClientMessage(playerid, -1, "{F39C12}[Phone]{FFFFFF} That player is already in another call.");
                SvdShowPhoneMenu(playerid);
                return 1;
            }
            new nowTs = gettime();
            if (gSvdCallIncomingFrom[targetid] != INVALID_PLAYER_ID)
            {
                if (!IsPlayerConnected(gSvdCallIncomingFrom[targetid]) || (nowTs - gSvdCallIncomingAt[targetid]) > 30)
                {
                    gSvdCallIncomingFrom[targetid] = INVALID_PLAYER_ID;
                    gSvdCallIncomingAt[targetid] = 0;
                }
                else
                {
                    SendClientMessage(playerid, -1, "{F39C12}[Phone]{FFFFFF} That player already has a pending call invite.");
                    SvdShowPhoneMenu(playerid);
                    return 1;
                }
            }
            if (gSvdLastCallSentAt[playerid] != 0 && (nowTs - gSvdLastCallSentAt[playerid]) < 10)
            {
                new msgCooldown[96];
                format(msgCooldown, sizeof(msgCooldown), "{F39C12}[Phone]{FFFFFF} Wait %d s before sending another call invite.", 10 - (nowTs - gSvdLastCallSentAt[playerid]));
                SendClientMessage(playerid, -1, msgCooldown);
                SvdShowPhoneMenu(playerid);
                return 1;
            }
            gSvdLastCallSentAt[playerid] = nowTs;
            gSvdCallIncomingFrom[targetid] = playerid;
            gSvdCallIncomingAt[targetid] = nowTs;
            new pName[24];
            SvdGetSafeName(playerid, pName, sizeof(pName));
            format(gSvdDlg, sizeof(gSvdDlg), "%s (%d) is calling you via SampVoice Plus.\nAccept voice call?", pName, playerid);
            SvdShowDlg(targetid, SVD_DLG_PHONE_INCOMING, DIALOG_STYLE_MSGBOX, "{FFE08A}Incoming Call", gSvdDlg, "Accept", "Decline");
            SendClientMessage(playerid, -1, "{85C1E9}[Phone]{FFFFFF} Ringing...");
            return 1;
        }

        case SVD_DLG_PHONE_INCOMING:
        {
            new caller = gSvdCallIncomingFrom[playerid];
            new sentAt = gSvdCallIncomingAt[playerid];
            gSvdCallIncomingFrom[playerid] = INVALID_PLAYER_ID;
            gSvdCallIncomingAt[playerid] = 0;
            if (caller == INVALID_PLAYER_ID || !IsPlayerConnected(caller) || sentAt == 0 || (gettime() - sentAt) > 30)
            {
                SendClientMessage(playerid, -1, "{F39C12}[Phone]{FFFFFF} That call invite expired.");
                return 1;
            }
            if (response)
            {
                SvdAcceptPhoneCall(playerid, caller);
            }
            else
            {
                SendClientMessage(caller, -1, "{F39C12}[Phone]{FFFFFF} Your call was declined.");
            }
            return 1;
        }

        case SVD_DLG_MEGAPHONE:
        {
            if (!response || listitem == 1) { SvdShowMainMenu(playerid); return 1; }
            SvdToggleMegaphone(playerid, gSvdMegaStream[playerid] == SV_NULL);
            SvdShowMegaphoneMenu(playerid);
            return 1;
        }

        case SVD_DLG_INTERCOM:
        {
            if (!response || listitem == 2) { SvdShowMainMenu(playerid); return 1; }
            if (listitem == 0 && GetPlayerVehicleID(playerid) > 0)
            {
                if (gSvdIntercomEnabled[playerid]) SvdStopIntercom(playerid);
                else SvdStartIntercom(playerid);
            }
            else if (listitem == 1)
            {
                gSvdIntercomDlMode[playerid] = !gSvdIntercomDlMode[playerid];
                if (gSvdIntercomEnabled[playerid]) SvdStartIntercom(playerid);
            }
            SvdShowIntercomMenu(playerid);
            return 1;
        }

        case SVD_DLG_SPEAKERBOX:
        {
            if (!response || listitem == 7) { SvdShowMainMenu(playerid); return 1; }
            new SV_LPSTREAM:st = gSvdBoxStream[playerid];
            switch (listitem)
            {
                case 0: SvdPlaceSpeakerBox(playerid);
                case 1:
                {
                    if (st != SV_NULL && SvIsValidStream(st))
                    {
                        new Float:x, Float:y, Float:z;
                        GetPlayerPos(playerid, x, y, z);
                        SvUpdatePositionForLPStream(st, x, y, z);
                        if (gSvdBoxObj[playerid] != INVALID_OBJECT_ID)
                        {
                            SetObjectPos(gSvdBoxObj[playerid], x + 0.8, y, z - 0.65);
                        }
                    }
                }
                case 2:
                {
                    gSvdBoxWorldFiltered[playerid] = !gSvdBoxWorldFiltered[playerid];
                    if (st != SV_NULL && SvIsValidStream(st) && !gSvdBoxSlMode[playerid])
                    {
                        if (gSvdBoxWorldFiltered[playerid])
                            SvStreamSetWorld(st, GetPlayerVirtualWorld(playerid), GetPlayerInterior(playerid));
                        else
                            SvStreamSetWorld(st, -1, -1);
                    }
                }
                case 3:
                {
                    if (st != SV_NULL && SvIsValidStream(st))
                    {
                        SvDetachAllListenersFromStream(st);
                        SvDetachAllSpeakersFromStream(st);
                        if (!gSvdBoxSlMode[playerid])
                        {
                            SendClientMessage(playerid, -1,
                                "{F39C12}[Box]{FFFFFF} Detached listeners & speakers; in DL mode, players in range re-attach in ~30 ms (switch to SL mode to keep them kicked).");
                        }
                        else
                        {
                            SendClientMessage(playerid, -1, "{2ECC71}[Box]{FFFFFF} Detached all SL listeners and speakers.");
                        }
                    }
                }
                case 4:
                {
                    if (st != SV_NULL && SvIsValidStream(st))
                    {
                        new Float:x, Float:y, Float:z, sType[20], msg[144], ids[8], pName[24];
                        SvdGetStreamTypeLabel(st, sType, sizeof(sType));
                        SvGetStreamPosition(st, x, y, z);
                        new cnt = SvGetStreamListeners(st, ids, sizeof(ids));
                        format(msg, sizeof(msg), "{2ECC71}[Box]{FFFFFF} Valid:%s Type:%s Pos:(%.1f,%.1f,%.1f) Listeners(%d):",
                               SvIsValidStream(st) ? "true" : "false", sType, x, y, z, cnt);
                        for (new li = 0; li < cnt && li < 4; li++)
                        {
                            SvdGetSafeName(ids[li], pName, sizeof(pName));
                            if (li > 0) strcat(msg, ",", sizeof(msg));
                            strcat(msg, " ", sizeof(msg));
                            strcat(msg, pName, sizeof(msg));
                        }
                        SendClientMessage(playerid, -1, msg);
                    }
                }
                case 5:
                {
                    gSvdBoxSlMode[playerid] = !gSvdBoxSlMode[playerid];
                    if (st != SV_NULL) SvdPlaceSpeakerBox(playerid);
                }
                case 6: SvdDestroySpeakerBox(playerid);
            }
            SvdShowSpeakerBoxMenu(playerid);
            return 1;
        }

        case SVD_DLG_OBJSOUND:
        {
            if (!response || listitem == 5) { SvdShowMainMenu(playerid); return 1; }
            switch (listitem)
            {
                case 0: SvdCreateObjSound(playerid);
                case 1:
                {
                    if (gSvdObjId[playerid] != INVALID_OBJECT_ID && IsValidObject(gSvdObjId[playerid]))
                    {
                        gSvdObjMoved[playerid] = !gSvdObjMoved[playerid];
                        new Float:tx = gSvdObjMoved[playerid] ? (gSvdObjOriginX[playerid] + 8.0) : gSvdObjOriginX[playerid];
                        MoveObject(gSvdObjId[playerid], tx, gSvdObjOriginY[playerid], gSvdObjOriginZ[playerid], 2.5);
                    }
                }
                case 2:
                {
                    gSvdObjSlMode[playerid] = !gSvdObjSlMode[playerid];
                    if (gSvdObjStream[playerid] != SV_NULL) SvdCreateObjSound(playerid);
                }
                case 3: SvdDestroyObjSound(playerid);
            }
            SvdShowObjSoundMenu(playerid);
            return 1;
        }

        case SVD_DLG_PARAMS:
        {
            if (!response || listitem == 8) { SvdShowMainMenu(playerid); return 1; }
            if (listitem == 0) { SvdShowLabStreamPicker(playerid, SVD_DLG_PARAMS_PICK_STREAM); return 1; }
            new SV_STREAM:st = SvdResolveLabStream(playerid);
            if (st == SV_NULL || !SvIsValidStream(st))
            {
                SendClientMessage(playerid, -1, "{F39C12}[Params Lab]{FFFFFF} Target stream is not active. Turn on Proximity or pick another stream.");
                SvdShowParamsMenu(playerid);
                return 1;
            }
            switch (listitem)
            {
                case 1:
                {
                    new Float:cur = SvStreamParameterGet(st, SV_PARAMETER_VOLUME);
                    new Float:nextV = (cur < 0.75) ? 1.0 : ((cur < 1.25) ? 1.5 : 0.5);
                    SvStreamParameterSet(st, SV_PARAMETER_VOLUME, nextV);
                }
                case 2: SvStreamParameterSlideFromTo(st, SV_PARAMETER_VOLUME, 0.1, 1.0, 800);
                case 3: SvStreamParameterSlideTo(st, SV_PARAMETER_VOLUME, 0.15, 800);
                case 4: SvStreamParameterSlide(st, SV_PARAMETER_VOLUME, 0.25, 400);
                case 5:
                {
                    new Float:pan = 0.0;
                    SvStreamParameterGetEx(st, SV_PARAMETER_PANNING, pan);
                    new Float:nextP = (pan < -0.2) ? 0.0 : ((pan < 0.2) ? 0.8 : -0.8);
                    SvStreamParameterSet(st, SV_PARAMETER_PANNING, nextP);
                }
                case 6:
                {
                    new Float:fr = 0.0;
                    SvStreamParameterGetEx(st, SV_PARAMETER_FREQUENCY, fr);
                    new Float:nextF = (fr < 1000.0) ? 36000.0 : ((fr < 49000.0) ? 60000.0 : 0.0);
                    SvStreamParameterSet(st, SV_PARAMETER_FREQUENCY, nextF);
                }
                case 7:
                {
                    SvStreamParameterReset(st, SV_PARAMETER_VOLUME);
                    SvStreamParameterReset(st, SV_PARAMETER_PANNING);
                    SvStreamParameterReset(st, SV_PARAMETER_FREQUENCY);
                }
            }
            SvdShowParamsMenu(playerid);
            return 1;
        }

        case SVD_DLG_PARAMS_PICK_STREAM:
        {
            if (response && listitem >= 0 && listitem < 9) gSvdLabTarget[playerid] = listitem;
            SvdShowParamsMenu(playerid);
            return 1;
        }

        case SVD_DLG_EFFECTS:
        {
            if (!response || listitem == SVD_FX_COUNT + 1) { SvdShowMainMenu(playerid); return 1; }
            if (listitem == 0) { SvdShowLabStreamPicker(playerid, SVD_DLG_EFFECTS_PICK_STREAM); return 1; }
            gSvdSelectedLabFx[playerid] = listitem - 1;
            SvdShowEffectItemMenu(playerid);
            return 1;
        }

        case SVD_DLG_EFFECTS_PICK_STREAM:
        {
            if (response && listitem >= 0 && listitem < 9)
            {
                new SV_STREAM:oldSt = SvdResolveLabStream(playerid);
                gSvdLabTarget[playerid] = listitem;
                new SV_STREAM:newSt = SvdResolveLabStream(playerid);
                SvdReattachLabEffects(playerid, oldSt, newSt);
            }
            SvdShowEffectsMenu(playerid);
            return 1;
        }

        case SVD_DLG_EFFECTS_ITEM:
        {
            if (!response || listitem == 2) { SvdShowEffectsMenu(playerid); return 1; }
            new e = gSvdSelectedLabFx[playerid];
            new SV_STREAM:st = SvdResolveLabStream(playerid);
            if (listitem == 0)
            {
                if (gSvdLabFx[playerid][e] != SV_NULL)
                {
                    if (st != SV_NULL && SvIsValidStream(st)) SvEffectDetachStream(gSvdLabFx[playerid][e], st);
                    SvEffectDelete(gSvdLabFx[playerid][e]);
                    gSvdLabFx[playerid][e] = SV_NULL;
                }
                else
                {
                    if (st == SV_NULL || !SvIsValidStream(st))
                    {
                        SendClientMessage(playerid, -1, "{F39C12}[Effects Lab]{FFFFFF} Turn on Proximity or choose an active target stream first.");
                    }
                    else
                    {
                        gSvdLabFx[playerid][e] = SvdCreatePresetEffect(e, gSvdLabFxStrong[playerid][e]);
                        if (gSvdLabFx[playerid][e] == SV_NULL) SvdNotifyErr(playerid, "SvEffectCreate*");
                        else SvEffectAttachStream(gSvdLabFx[playerid][e], st);
                    }
                }
            }
            else if (listitem == 1)
            {
                gSvdLabFxStrong[playerid][e] = !gSvdLabFxStrong[playerid][e];
                if (gSvdLabFx[playerid][e] != SV_NULL && SvIsValidEffect(gSvdLabFx[playerid][e]))
                {
                    SvdUpdatePresetEffect(gSvdLabFx[playerid][e], e, gSvdLabFxStrong[playerid][e]);
                }
            }
            SvdShowEffectItemMenu(playerid);
            return 1;
        }

        case SVD_DLG_PRIVACY:
        {
            if (!response) { SvdShowMainMenu(playerid); return 1; }
            new bool:admin = !!(IsPlayerAdmin(playerid));
            if ((!admin && listitem == 2) || (admin && listitem == 4)) { SvdShowMainMenu(playerid); return 1; }

            if (listitem == 0)
            {
                format(gSvdDlg, sizeof(gSvdDlg), "Enter target speaker ID to toggle server-side mute for you only:");
                SvdShowDlg(playerid, SVD_DLG_PRIVACY_PAIR_INPUT, DIALOG_STYLE_INPUT, "{FFE08A}Pair Mute (For Me)", gSvdDlg, "Toggle", "Back");
                return 1;
            }
            if (listitem == 1)
            {
                if (SvCheckListener(playerid))
                {
                    SvDisableListener(playerid);
                    gSvdFsDisabledListener[playerid] = true;
                }
                else
                {
                    SvEnableListener(playerid);
                    gSvdFsDisabledListener[playerid] = false;
                }
                SvdShowPrivacyMenu(playerid);
                return 1;
            }
            if (admin && listitem == 2)
            {
                format(gSvdDlg, sizeof(gSvdDlg), "Enter target player ID (toggles 30 s server mute with key-release / unmute):");
                SvdShowDlg(playerid, SVD_DLG_PRIVACY_MUTE_INPUT, DIALOG_STYLE_INPUT, "{FFE08A}[Admin] Timed Server Mute", gSvdDlg, "Apply", "Back");
                return 1;
            }
            if (admin && listitem == 3)
            {
                gSvdFsToggledDebug = !gSvdFsToggledDebug;
                SvDebug(gSvdFsToggledDebug);
                SvdShowPrivacyMenu(playerid);
                return 1;
            }
            return 1;
        }

        case SVD_DLG_PRIVACY_PAIR_INPUT:
        {
            if (!response) { SvdShowPrivacyMenu(playerid); return 1; }
            new targetid = INVALID_PLAYER_ID;
            if (!SvdParseIntStrict(inputtext, targetid) || targetid == playerid || !IsPlayerConnected(targetid))
            {
                SendClientMessage(playerid, -1, "{E74C3C}[Privacy]{FFFFFF} Invalid or offline player ID.");
            }
            else if (SvIsPlayerMutedForListener(targetid, playerid))
            {
                SvUnmutePlayerForListener(targetid, playerid);
                SvdSetPairBit(playerid, targetid, false);
                SendClientMessage(playerid, -1, "{2ECC71}[Privacy]{FFFFFF} Unmuted that player for you.");
            }
            else if (SvMutePlayerForListener(targetid, playerid))
            {
                SvdSetPairBit(playerid, targetid, true);
                SendClientMessage(playerid, -1, "{F39C12}[Privacy]{FFFFFF} Muted that player for you (server-side).");
            }
            SvdShowPrivacyMenu(playerid);
            return 1;
        }

        case SVD_DLG_PRIVACY_MUTE_INPUT:
        {
            if (!response || !IsPlayerAdmin(playerid)) { SvdShowPrivacyMenu(playerid); return 1; }
            new targetid = INVALID_PLAYER_ID;
            if (!SvdParseIntStrict(inputtext, targetid) || !IsPlayerConnected(targetid))
            {
                SendClientMessage(playerid, -1, "{E74C3C}[Admin]{FFFFFF} Invalid or offline player ID.");
            }
            else if (SvMutePlayerStatus(targetid))
            {
                new rem = SvMutePlayerRemaining(targetid);
                SvMutePlayerDisable(targetid);
                gSvdFsServerMuted[targetid] = false;
                new mMsg[96];
                format(mMsg, sizeof(mMsg), "{2ECC71}[Admin]{FFFFFF} Player unmuted (was %d ms remaining).", rem);
                SendClientMessage(playerid, -1, mMsg);
            }
            else if (SvMutePlayerEnable(targetid, true, 30000))
            {
                gSvdFsServerMuted[targetid] = true;
                SendClientMessage(playerid, -1, "{F39C12}[Admin]{FFFFFF} Player muted for 30 s (releasekeys = true).");
            }
            SvdShowPrivacyMenu(playerid);
            return 1;
        }

        case SVD_DLG_EXPLORER:
        {
            if (!response || listitem == 9) { SvdShowMainMenu(playerid); return 1; }
            gSvdSelectedExplorerIdx[playerid] = listitem;
            SvdShowExplorerDetail(playerid, listitem);
            return 1;
        }

        case SVD_DLG_EXPLORER_DETAIL:
        {
            if (response) SvdShowExplorerList(playerid); else SvdShowMainMenu(playerid);
            return 1;
        }

        case SVD_DLG_LIVE_EVENTS:
        {
            if (!response) { SvdShowMainMenu(playerid); return 1; }
            new bool:admin = !!(IsPlayerAdmin(playerid));
            if ((!admin && listitem == 1) || (admin && listitem == 2)) { SvdShowMainMenu(playerid); return 1; }
            if (listitem == 0) gSvdFeedSelf[playerid] = !gSvdFeedSelf[playerid];
            else if (admin && listitem == 1) gSvdFeedAll[playerid] = !gSvdFeedAll[playerid];
            SvdShowLiveEventsMenu(playerid);
            return 1;
        }

        case SVD_DLG_ABOUT:
        {
            if (response) SvdShowMainMenu(playerid);
            return 1;
        }

        case SVD_DLG_KEYBINDS:
        {
            if (response) SvdShowRadioDetailMenu(playerid);
            return 1;
        }

        case SVD_DLG_SERVER_STATS:
        {
            if (response)
            {
                if (IsPlayerAdmin(playerid)) SvdShowServerStatsDialog(playerid);
                else SvdShowMainMenu(playerid);
            }
            else SvdShowMainMenu(playerid);
            return 1;
        }
    }
    return 0;
}

// =============================================================================
// 10. SampVoice Plus Callbacks (React only to FS-owned keys/streams)
// =============================================================================

public SV_VOID:OnPlayerActivationKeyPress(SV_UINT:playerid, SV_UINT:keyid)
{
    new pName[24], ev[112];
    SvdGetSafeName(playerid, pName, sizeof(pName));
    format(ev, sizeof(ev), "%s(%d) KeyPress 0x%02X", pName, playerid, keyid);
    SvdEmitEvent(playerid, ev);

    if (SvMutePlayerStatus(playerid)) return;

    if (keyid == SVD_KEY_LOCAL)
    {
        if (gSvdProxStream[playerid] != SV_NULL && SvIsValidStream(gSvdProxStream[playerid]))
            SvAttachSpeakerToStream(gSvdProxStream[playerid], playerid);

        if (gSvdIntercomEnabled[playerid] && gSvdIntercomStream[playerid] != SV_NULL && SvIsValidStream(gSvdIntercomStream[playerid]))
            SvAttachSpeakerToStream(gSvdIntercomStream[playerid], playerid);

        if (gSvdBoxStream[playerid] != SV_NULL && SvIsValidStream(gSvdBoxStream[playerid]))
            SvAttachSpeakerToStream(gSvdBoxStream[playerid], playerid);

        if (gSvdObjStream[playerid] != SV_NULL && SvIsValidStream(gSvdObjStream[playerid]))
            SvAttachSpeakerToStream(gSvdObjStream[playerid], playerid);
    }
    else if (keyid == SVD_KEY_RADIO)
    {
        if (gSvdRadioChan[playerid] >= 0 && gSvdRadioStream[gSvdRadioChan[playerid]] != SV_NULL &&
            !SvIsKeyBoundToStream(playerid, SVD_KEY_RADIO, gSvdRadioStream[gSvdRadioChan[playerid]]))
            SvAttachSpeakerToStream(gSvdRadioStream[gSvdRadioChan[playerid]], playerid);

        if (gSvdCallStream[playerid] != SV_NULL && SvIsValidStream(gSvdCallStream[playerid]) &&
            !SvHasSpeakerInStream(gSvdCallStream[playerid], playerid))
            SvAttachSpeakerToStream(gSvdCallStream[playerid], playerid);
    }
    else if (keyid == SVD_KEY_MEGA)
    {
        if (gSvdMegaStream[playerid] != SV_NULL && SvIsValidStream(gSvdMegaStream[playerid]))
            SvAttachSpeakerToStream(gSvdMegaStream[playerid], playerid);
    }
}

public SV_VOID:OnPlayerActivationKeyRelease(SV_UINT:playerid, SV_UINT:keyid)
{
    new pName[24], ev[112];
    SvdGetSafeName(playerid, pName, sizeof(pName));
    format(ev, sizeof(ev), "%s(%d) KeyRelease 0x%02X", pName, playerid, keyid);
    SvdEmitEvent(playerid, ev);

    if (keyid == SVD_KEY_LOCAL)
    {
        if (gSvdProxStream[playerid] != SV_NULL && SvHasSpeakerInStream(gSvdProxStream[playerid], playerid))
            SvDetachSpeakerFromStream(gSvdProxStream[playerid], playerid);

        if (gSvdIntercomStream[playerid] != SV_NULL && SvHasSpeakerInStream(gSvdIntercomStream[playerid], playerid))
            SvDetachSpeakerFromStream(gSvdIntercomStream[playerid], playerid);

        if (gSvdBoxStream[playerid] != SV_NULL && SvHasSpeakerInStream(gSvdBoxStream[playerid], playerid))
            SvDetachSpeakerFromStream(gSvdBoxStream[playerid], playerid);

        if (gSvdObjStream[playerid] != SV_NULL && SvHasSpeakerInStream(gSvdObjStream[playerid], playerid))
            SvDetachSpeakerFromStream(gSvdObjStream[playerid], playerid);
    }
    else if (keyid == SVD_KEY_RADIO)
    {
        if (gSvdRadioChan[playerid] >= 0 && gSvdRadioStream[gSvdRadioChan[playerid]] != SV_NULL &&
            !SvIsKeyBoundToStream(playerid, SVD_KEY_RADIO, gSvdRadioStream[gSvdRadioChan[playerid]]) &&
            SvHasSpeakerInStream(gSvdRadioStream[gSvdRadioChan[playerid]], playerid))
            SvDetachSpeakerFromStream(gSvdRadioStream[gSvdRadioChan[playerid]], playerid);
    }
    else if (keyid == SVD_KEY_MEGA)
    {
        if (gSvdMegaStream[playerid] != SV_NULL && SvHasSpeakerInStream(gSvdMegaStream[playerid], playerid))
            SvDetachSpeakerFromStream(gSvdMegaStream[playerid], playerid);
    }
}

public SV_VOID:OnPlayerMicroStateChange(SV_UINT:playerid, bool:hasMicro)
{
    new pName[24], ev[96];
    SvdGetSafeName(playerid, pName, sizeof(pName));
    format(ev, sizeof(ev), "%s(%d) MicroStateChange -> %s", pName, playerid, hasMicro ? "available" : "unavailable");
    SvdEmitEvent(playerid, ev);
}

public SV_VOID:OnPlayerVoiceMute(SV_UINT:playerid, bool:releasekeys, SV_UINT:time, bool:speaking)
{
    new pName[24], ev[120];
    SvdGetSafeName(playerid, pName, sizeof(pName));
    format(ev, sizeof(ev), "%s(%d) VoiceMute (rel:%d time:%u spk:%d)", pName, playerid, releasekeys ? 1 : 0, time, speaking ? 1 : 0);
    SvdEmitEvent(playerid, ev);
}

public SV_VOID:OnPlayerVoiceUnmute(SV_UINT:playerid, bool:expired)
{
    if (gSvdFsServerMuted[playerid]) gSvdFsServerMuted[playerid] = false;
    new pName[24], ev[96];
    SvdGetSafeName(playerid, pName, sizeof(pName));
    format(ev, sizeof(ev), "%s(%d) VoiceUnmute (expired:%d)", pName, playerid, expired ? 1 : 0);
    SvdEmitEvent(playerid, ev);
}

public SV_VOID:OnPlayerVoiceReady(SV_UINT:playerid)
{
    // Attach late joiners to any active static-listener (SL) demo streams
    for (new owner = 0; owner < MAX_PLAYERS; owner++)
    {
        if (owner == playerid || !IsPlayerConnected(owner)) continue;
        if (gSvdProxSlMode[owner] && gSvdProxStream[owner] != SV_NULL && SvIsValidStream(gSvdProxStream[owner]) &&
            !SvHasListenerInStream(gSvdProxStream[owner], playerid))
        {
            SvAttachListenerToStream(gSvdProxStream[owner], playerid);
        }
        if (gSvdBoxSlMode[owner] && gSvdBoxStream[owner] != SV_NULL && SvIsValidStream(gSvdBoxStream[owner]) &&
            !SvHasListenerInStream(gSvdBoxStream[owner], playerid))
        {
            SvAttachListenerToStream(gSvdBoxStream[owner], playerid);
        }
        if (gSvdObjSlMode[owner] && gSvdObjStream[owner] != SV_NULL && SvIsValidStream(gSvdObjStream[owner]) &&
            !SvHasListenerInStream(gSvdObjStream[owner], playerid))
        {
            SvAttachListenerToStream(gSvdObjStream[owner], playerid);
        }
    }

    new pName[24], ev[96];
    SvdGetSafeName(playerid, pName, sizeof(pName));
    format(ev, sizeof(ev), "%s(%d) VoiceReady", pName, playerid);
    SvdEmitEvent(playerid, ev);
}

public SV_VOID:OnPlayerVoiceStart(SV_UINT:playerid)
{
    new pName[24], ev[96];
    SvdGetSafeName(playerid, pName, sizeof(pName));
    format(ev, sizeof(ev), "%s(%d) VoiceStart", pName, playerid);
    SvdEmitEvent(playerid, ev);
}

public SV_VOID:OnPlayerVoiceStop(SV_UINT:playerid)
{
    new pName[24], ev[96];
    SvdGetSafeName(playerid, pName, sizeof(pName));
    format(ev, sizeof(ev), "%s(%d) VoiceStop", pName, playerid);
    SvdEmitEvent(playerid, ev);
}
