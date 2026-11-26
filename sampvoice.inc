/*
 * SampVoice Plus
 * Version: 1.2.0
 * Repository: https://github.com/Walter-Correa/SampVoice-Plus
 * Documentation: docs/api-reference.md
 */

#if defined _sampvoice_included
    #endinput
#endif
#define _sampvoice_included

// -----------------------------------------------------------------------------
// Version
// -----------------------------------------------------------------------------

#define SV_PLUS_VERSION_MAJOR   1
#define SV_PLUS_VERSION_MINOR   2
#define SV_PLUS_VERSION_PATCH   0
#define SV_PLUS_VERSION_NUMBER  10200
#define SV_PLUS_VERSION_STRING  "1.2.0"

// -----------------------------------------------------------------------------
// Constants & Tags
// -----------------------------------------------------------------------------

#define SV_VERSION      11
#define SV_PLUS_VERSION 12

#define SV_CAP_ADAPTIVE_JITTER  (1 << 1)
#define SV_CAP_TELEMETRY        (1 << 2)

#define SV_VOICE_STATUS_OK          0
#define SV_VOICE_STATUS_NO_MICRO    1
#define SV_VOICE_STATUS_CANNOT_HEAR 2

#define SV_NULL         0
#define SV_INFINITY     -1
#define SV_FALSE        false
#define SV_TRUE         true

#define SV_PTR:
#define SV_STR:
#define SV_INT:
#define SV_UINT:
#define SV_FLOAT:       Float:
#define SV_BOOL:        bool:
#define SV_VOID:

#define SV_STREAM:      SV_PTR:
#define SV_GSTREAM:     SV_PTR:
#define SV_LSTREAM:     SV_PTR:
#define SV_LPSTREAM:    SV_PTR:
#define SV_SLSTREAM:    SV_PTR:
#define SV_DLSTREAM:    SV_PTR:
#define SV_EFFECT:      SV_PTR:

// -----------------------------------------------------------------------------
// Enums
// -----------------------------------------------------------------------------

enum SV_PARAMETER
{
    SV_PARAMETER_FREQUENCY = 1,
    SV_PARAMETER_VOLUME    = 2,
    SV_PARAMETER_PANNING   = 3,
    SV_PARAMETER_EAXMIX    = 4,
    SV_PARAMETER_SRC       = 8
}

// -----------------------------------------------------------------------------
// Core & Protocol
// -----------------------------------------------------------------------------

native SV_VOID:SvDebug(SV_BOOL:mode);
native SV_VOID:SvInit(SV_UINT:bitrate);
native SV_UINT:SvGetVersion(SV_UINT:playerid);
native SV_UINT:SvGetClientProtocol(SV_UINT:playerid);
native SV_UINT:SvGetPlusCapabilities(SV_UINT:playerid);
native SV_UINT:SvGetPlusClientVersion(SV_UINT:playerid);
native SV_BOOL:SvIsPlayerVoiceReady(SV_UINT:playerid);

// -----------------------------------------------------------------------------
// Microphone & Recording
// -----------------------------------------------------------------------------

native SV_BOOL:SvHasMicro(SV_UINT:playerid);
native SV_BOOL:SvIsPlayerSpeaking(SV_UINT:playerid);
native SV_INT:SvGetPlayerVoiceStatus(SV_UINT:playerid);
native SV_BOOL:SvStartRecord(SV_UINT:playerid);
native SV_BOOL:SvStopRecord(SV_UINT:playerid);

// -----------------------------------------------------------------------------
// Activation Keys
// -----------------------------------------------------------------------------

native SV_BOOL:SvAddKey(SV_UINT:playerid, SV_UINT:keyid);
native SV_BOOL:SvHasKey(SV_UINT:playerid, SV_UINT:keyid);
native SV_BOOL:SvRemoveKey(SV_UINT:playerid, SV_UINT:keyid);
native SV_VOID:SvRemoveAllKeys(SV_UINT:playerid);

// -----------------------------------------------------------------------------
// Player Mute & Listener
// -----------------------------------------------------------------------------

native SV_BOOL:SvMutePlayerStatus(SV_UINT:playerid);
native SV_VOID:SvMutePlayerEnable(SV_UINT:playerid, SV_BOOL:releasekeys = SV_FALSE, SV_UINT:time = 0);
native SV_VOID:SvMutePlayerDisable(SV_UINT:playerid);
native SV_INT:SvMutePlayerRemaining(SV_UINT:playerid);
native SV_BOOL:SvMutePlayerForListener(SV_UINT:speakerid, SV_UINT:listenerid);
native SV_BOOL:SvUnmutePlayerForListener(SV_UINT:speakerid, SV_UINT:listenerid);
native SV_BOOL:SvIsPlayerMutedForListener(SV_UINT:speakerid, SV_UINT:listenerid);
native SV_BOOL:SvEnableListener(SV_UINT:playerid);
native SV_BOOL:SvDisableListener(SV_UINT:playerid);
native SV_BOOL:SvCheckListener(SV_UINT:playerid);

// -----------------------------------------------------------------------------
// Stream Creation
// -----------------------------------------------------------------------------

native SV_GSTREAM:SvCreateGStream(SV_UINT:color = SV_NULL, const SV_STR:name[] = "");
native SV_LPSTREAM:SvCreateSLStreamAtPoint(SV_FLOAT:distance, SV_FLOAT:posx, SV_FLOAT:posy, SV_FLOAT:posz, SV_UINT:color = SV_NULL, const SV_STR:name[] = "");
native SV_SLSTREAM:SvCreateSLStreamAtVehicle(SV_FLOAT:distance, SV_UINT:vehicleid, SV_UINT:color = SV_NULL, const SV_STR:name[] = "");
native SV_SLSTREAM:SvCreateSLStreamAtPlayer(SV_FLOAT:distance, SV_UINT:playerid, SV_UINT:color = SV_NULL, const SV_STR:name[] = "");
native SV_SLSTREAM:SvCreateSLStreamAtObject(SV_FLOAT:distance, SV_UINT:objectid, SV_UINT:color = SV_NULL, const SV_STR:name[] = "");
native SV_LPSTREAM:SvCreateDLStreamAtPoint(SV_FLOAT:distance, SV_UINT:maxplayers, SV_FLOAT:posx, SV_FLOAT:posy, SV_FLOAT:posz, SV_UINT:color = SV_NULL, const SV_STR:name[] = "");
native SV_DLSTREAM:SvCreateDLStreamAtVehicle(SV_FLOAT:distance, SV_UINT:maxplayers, SV_UINT:vehicleid, SV_UINT:color = SV_NULL, const SV_STR:name[] = "");
native SV_DLSTREAM:SvCreateDLStreamAtPlayer(SV_FLOAT:distance, SV_UINT:maxplayers, SV_UINT:playerid, SV_UINT:color = SV_NULL, const SV_STR:name[] = "");
native SV_DLSTREAM:SvCreateDLStreamAtObject(SV_FLOAT:distance, SV_UINT:maxplayers, SV_UINT:objectid, SV_UINT:color = SV_NULL, const SV_STR:name[] = "");

// -----------------------------------------------------------------------------
// Stream Control
// -----------------------------------------------------------------------------

native SV_VOID:SvUpdateDistanceForLStream(SV_LSTREAM:lstream, SV_FLOAT:distance);
native SV_VOID:SvUpdatePositionForLPStream(SV_LPSTREAM:lpstream, SV_FLOAT:posx, SV_FLOAT:posy, SV_FLOAT:posz);
native SV_BOOL:SvStreamSetIcon(SV_STREAM:stream, const SV_STR:icon[]);
native SV_VOID:SvDeleteStream(SV_STREAM:stream);
native SV_BOOL:SvIsValidStream(SV_STREAM:stream);

// -----------------------------------------------------------------------------
// Stream Listeners & Speakers
// -----------------------------------------------------------------------------

native SV_BOOL:SvAttachListenerToStream(SV_STREAM:stream, SV_UINT:playerid, SV_FLOAT:volume = 1.0);
native SV_BOOL:SvStreamSetListenerVolume(SV_STREAM:stream, SV_UINT:playerid, SV_FLOAT:volume);
native SV_FLOAT:SvStreamGetListenerVolume(SV_STREAM:stream, SV_UINT:playerid);
native SV_INT:SvGetStreamListenerCount(SV_STREAM:stream);
native SV_BOOL:SvHasListenerInStream(SV_STREAM:stream, SV_UINT:playerid);
native SV_BOOL:SvDetachListenerFromStream(SV_STREAM:stream, SV_UINT:playerid);
native SV_VOID:SvDetachAllListenersFromStream(SV_STREAM:stream);
native SV_BOOL:SvAttachSpeakerToStream(SV_STREAM:stream, SV_UINT:playerid);
native SV_INT:SvGetStreamSpeakerCount(SV_STREAM:stream);
native SV_BOOL:SvHasSpeakerInStream(SV_STREAM:stream, SV_UINT:playerid);
native SV_BOOL:SvDetachSpeakerFromStream(SV_STREAM:stream, SV_UINT:playerid);
native SV_VOID:SvDetachAllSpeakersFromStream(SV_STREAM:stream);

// -----------------------------------------------------------------------------
// Stream Parameters
// -----------------------------------------------------------------------------

native SV_VOID:SvStreamParameterSet(SV_STREAM:stream, SV_PARAMETER:parameter, SV_FLOAT:value);
native SV_VOID:SvStreamParameterReset(SV_STREAM:stream, SV_PARAMETER:parameter);
native SV_BOOL:SvStreamParameterHas(SV_STREAM:stream, SV_PARAMETER:parameter);
native SV_FLOAT:SvStreamParameterGet(SV_STREAM:stream, SV_PARAMETER:parameter);
native SV_VOID:SvStreamParameterSlideFromTo(SV_STREAM:stream, SV_PARAMETER:parameter, SV_FLOAT:startvalue, SV_FLOAT:endvalue, SV_UINT:time);
native SV_VOID:SvStreamParameterSlideTo(SV_STREAM:stream, SV_PARAMETER:parameter, SV_FLOAT:endvalue, SV_UINT:time);
native SV_VOID:SvStreamParameterSlide(SV_STREAM:stream, SV_PARAMETER:parameter, SV_FLOAT:deltavalue, SV_UINT:time);

// -----------------------------------------------------------------------------
// Audio Effects
// -----------------------------------------------------------------------------

native SV_EFFECT:SvEffectCreateChorus(SV_INT:priority, SV_FLOAT:wetdrymix, SV_FLOAT:depth, SV_FLOAT:feedback, SV_FLOAT:frequency, SV_UINT:waveform, SV_FLOAT:delay, SV_UINT:phase);
native SV_EFFECT:SvEffectCreateCompressor(SV_INT:priority, SV_FLOAT:gain, SV_FLOAT:attack, SV_FLOAT:release, SV_FLOAT:threshold, SV_FLOAT:ratio, SV_FLOAT:predelay);
native SV_EFFECT:SvEffectCreateDistortion(SV_INT:priority, SV_FLOAT:gain, SV_FLOAT:edge, SV_FLOAT:posteqcenterfrequency, SV_FLOAT:posteqbandwidth, SV_FLOAT:prelowpasscutoff);
native SV_EFFECT:SvEffectCreateEcho(SV_INT:priority, SV_FLOAT:wetdrymix, SV_FLOAT:feedback, SV_FLOAT:leftdelay, SV_FLOAT:rightdelay, SV_BOOL:pandelay);
native SV_EFFECT:SvEffectCreateFlanger(SV_INT:priority, SV_FLOAT:wetdrymix, SV_FLOAT:depth, SV_FLOAT:feedback, SV_FLOAT:frequency, SV_UINT:waveform, SV_FLOAT:delay, SV_UINT:phase);
native SV_EFFECT:SvEffectCreateGargle(SV_INT:priority, SV_UINT:ratehz, SV_UINT:waveshape);
native SV_EFFECT:SvEffectCreateI3dl2reverb(SV_INT:priority, SV_INT:room, SV_INT:roomhf, SV_FLOAT:roomrollofffactor, SV_FLOAT:decaytime, SV_FLOAT:decayhfratio, SV_INT:reflections, SV_FLOAT:reflectionsdelay, SV_INT:reverb, SV_FLOAT:reverbdelay, SV_FLOAT:diffusion, SV_FLOAT:density, SV_FLOAT:hfreference);
native SV_EFFECT:SvEffectCreateParameq(SV_INT:priority, SV_FLOAT:center, SV_FLOAT:bandwidth, SV_FLOAT:gain);
native SV_EFFECT:SvEffectCreateReverb(SV_INT:priority, SV_FLOAT:ingain, SV_FLOAT:reverbmix, SV_FLOAT:reverbtime, SV_FLOAT:highfreqrtratio);
native SV_VOID:SvEffectAttachStream(SV_EFFECT:effect, SV_STREAM:stream);
native SV_VOID:SvEffectDetachStream(SV_EFFECT:effect, SV_STREAM:stream);
native SV_VOID:SvEffectDelete(SV_EFFECT:effect);
native SV_BOOL:SvIsValidEffect(SV_EFFECT:effect);

// -----------------------------------------------------------------------------
// Callbacks
// -----------------------------------------------------------------------------

forward SV_VOID:OnPlayerActivationKeyPress(SV_UINT:playerid, SV_UINT:keyid);
forward SV_VOID:OnPlayerActivationKeyRelease(SV_UINT:playerid, SV_UINT:keyid);
forward SV_VOID:OnPlayerMicroStateChange(SV_UINT:playerid, bool:hasMicro);
forward SV_VOID:OnPlayerVoiceMute(SV_UINT:playerid, bool:releasekeys, SV_UINT:time, bool:speaking);
forward SV_VOID:OnPlayerVoiceUnmute(SV_UINT:playerid, bool:expired);
forward SV_VOID:OnPlayerVoiceReady(SV_UINT:playerid);
forward SV_VOID:OnPlayerVoiceStart(SV_UINT:playerid);
forward SV_VOID:OnPlayerVoiceStop(SV_UINT:playerid);
