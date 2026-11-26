# Audio Streams & DX8 DSP Effects

**SampVoice Plus** organizes all voice routing through **Streams** (`Global`, `Static Local`, `Dynamic Local`) combined with optional **Stream Parameters** and **DX8 Audio Effects** attached per stream.

---

## 1. Stream Types Overview

| Stream Type | Pawn Creation Natives | Spatial 3D Audio? | Auto-Listener Distance Management? | Typical Use Cases |
| :--- | :--- | :---: | :---: | :--- |
| **Global (`SV_GSTREAM`)** | `SvCreateGStream` | No (2D mono) | Manual (`SvAttachListenerToStream`) | Radio channels, admin announcements, phone calls, intercoms |
| **Static Local (`SV_LPSTREAM` / `SV_SLSTREAM`)** | `SvCreateSLStreamAtPoint`<br>`SvCreateSLStreamAtVehicle`<br>`SvCreateSLStreamAtPlayer`<br>`SvCreateSLStreamAtObject` | Yes (3D pos) | Manual (`SvAttachListenerToStream`) | Fixed VIP clubs, custom-managed proximity groups |
| **Dynamic Local (`SV_LPSTREAM` / `SV_DLSTREAM`)** | `SvCreateDLStreamAtPoint`<br>`SvCreateDLStreamAtVehicle`<br>`SvCreateDLStreamAtPlayer`<br>`SvCreateDLStreamAtObject` | Yes (3D pos) | **Automatic** via SpatialGrid (`maxplayers` cap within `distance`) | Proximity local voice chat, vehicle megaphones, sirens, stage mics |

* **Dynamic vs Static Local:**
  Use **`SvCreateDLStreamAt*`** for proximity chat where hundreds of players enter/leave range automatically (`maxplayers` caps simultaneous streams per listener, prioritized by distance). Use **`SvCreateSLStreamAt*`** when your Pawn script explicitly decides which players hear a 3D source.
* **Updating Position & Radius:**
  * `SvUpdateDistanceForLStream(lstream, distance)` adjusts audible radius at runtime for any local stream.
  * `SvUpdatePositionForLPStream(lpstream, x, y, z)` moves point-based streams (`AtPoint`). Player/vehicle/object-attached streams track their GTA entity automatically on every server update tick.

### Client-Side Spatial Attenuation & Stereo Panning

To keep SA-MP's global BASS 3D listener and positional streams (e.g., `PlayAudioStreamForPlayer`) untouched, **SampVoice Plus** computes local voice spatialization in software without creating `BASS_SAMPLE_3D` voice channels or calling `BASS_Set3DPosition` / `BASS_Apply3D`:

* **Listener Origin ($\mathbf{p}_{\text{listener}}$):** Uses the local ped position $\mathbf{p}_{\text{ped}}$ whenever available and within $20\text{ m}$ of the camera ($\lVert \mathbf{p}_{\text{cam}} - \mathbf{p}_{\text{ped}} \rVert \le 20.0$) so third-person camera offset does not attenuate close-range local speech; falls back to the camera position $\mathbf{p}_{\text{cam}}$ whenever the local ped position is unavailable or the camera is more than $20\text{ m}$ away (spectator mode, free camera, cutscenes).
* **Distance Gain ($g_{\text{spatial}}$):** Given listener-to-source distance $d = \lVert \mathbf{p}_{\text{source}} - \mathbf{p}_{\text{listener}} \rVert$, $d_{\max} = \max(1.0, \text{distance})$, and $d_{\min} = 0.20 \cdot d_{\max}$, the gain stays at full volume inside $d_{\min}$ and falls smoothly to zero with zero slope at $d_{\max}$:
  $$g_{\text{spatial}}(d) = \begin{cases} 1.0 & d \le d_{\min} \\ \left(\dfrac{d_{\max} - d}{d_{\max} - d_{\min}}\right)^{1.5} & d_{\min} < d < d_{\max} \\ 0.0 & d \ge d_{\max} \end{cases}$$
  Reference values on a $60\text{ m}$ local stream ($d_{\min} = 12\text{ m}$):

  | Distance $d$ | $10\text{ m}$ | $12\text{ m}$ | $15\text{ m}$ | $20\text{ m}$ | $30\text{ m}$ | $45\text{ m}$ | $55\text{ m}$ | $60\text{ m}$ |
  | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
  | Gain ($g_{\text{spatial}}$) | `1.000` | `1.000` | `0.908` | `0.761` | `0.494` | `0.175` | `0.034` | `0` |

* **Stereo Pan ($p_{\text{spatial}}$):** Computed from the camera right vector $\mathbf{r} = \widehat{\mathbf{f} \times \mathbf{t}}$ (with $\mathbf{f}$ from GTA SA `CMatrix::up` at `+0x10` and $\mathbf{t}$ from `CMatrix::at` at `+0x20`) and unit direction $\hat{\mathbf{u}} = \frac{\mathbf{p}_{\text{source}} - \mathbf{p}_{\text{listener}}}{d}$, capped at $\pm 0.5$ (so the far ear never drops below 50%) and tapered only within a fixed $3\text{ m}$ proximity radius:
  $$p_{\text{spatial}} = 0.5 \cdot \operatorname{clamp}(\hat{\mathbf{u}} \cdot \mathbf{r},\, -1.0,\, +1.0) \cdot \operatorname{clamp}\!\left(\frac{d}{3.0},\, 0.0,\, 1.0\right)$$
  Applied via `BASS_ATTRIB_PAN` with `50 ms` `BASS_ChannelSlideAttribute` smoothing between ticks.
* **Script Parameter Composition:** `SV_PARAMETER_VOLUME` multiplies ($g_{\text{final}} = \text{masterVol} \cdot g_{\text{stream}} \cdot g_{\text{spatial}}$) and `SV_PARAMETER_PANNING` adds ($p_{\text{final}} = \operatorname{clamp}(p_{\text{spatial}} + p_{\text{script}},\, -1.0,\, +1.0)$). **Global streams** use $g_{\text{spatial}} = 1.0$ and $p_{\text{spatial}} = 0.0$.
* **Legacy `v11` Clients:** Older `v11` clients evaluate volume through BASS 3D (`min = 0.1 * max`, rolloff `1.0` anchored at the camera). To approximate the softer falloff above, the server sends legacy `v11` listeners `distance * 2.0` (`kLegacyDistanceScale`) in `CreateLPStream`, `CreateLStreamAt*`, and `UpdateLStreamDistance` packets while keeping the real distance in the server spatial grid: on **dynamic (`DL`) streams**, the server detaches out-of-range listeners at the real distance (resulting in a cut at the boundary); on **static (`SL`) streams**, the server never detaches by distance and BASS 3D holds its floor volume beyond `max` instead of muting, so `v11` listeners still hear a quiet floor level beyond the real radius (compared to `0.0` on Plus `v12`).

---

## 2. Dynamic Stream Parameters & Sliding

Each stream exposes 5 controllable `SV_PARAMETER` attributes:

| Enum Constant | Value | Meaning | Default (`SvStreamParameterGet`) |
| :--- | :---: | :--- | :---: |
| `SV_PARAMETER_FREQUENCY` | `1` | Sample playback rate / pitch (Hz; `0.0` resets/uses original `48000` Hz stream rate) | `0.0` |
| `SV_PARAMETER_VOLUME` | `2` | Gain multiplier (`0.0` .. `1.0+`), combined with software spatial distance attenuation | `1.0` |
| `SV_PARAMETER_PANNING` | `3` | Stereo pan offset (`-1.0` left .. `1.0` right), added onto software spatial stereo pan | `0.0` |
| `SV_PARAMETER_EAXMIX` | `4` | Legacy script compatibility no-op (accepted cleanly, no audible effect; hardware EAX/BASS 3D is omitted to keep SA-MP audio intact) | `-1.0` |
| `SV_PARAMETER_SRC` | `8` | BASS sample interpolation quality | `1.0` |

### Spatialization & Parameter Transitions
* **Software 3D Spatialization (`SLStream` / `DLStream`):** Static and dynamic local streams resolve source coordinates (`Point`, `Vehicle`, `Player`, or `Object`) and the local `CCamera` basis once per render frame into an SEH-guarded snapshot, then compute smoothstep distance rolloff and stereo pan on every audio thread tick (~10 ms) without enabling BASS 3D mode.
* **Instant Parameters:** `SvStreamParameterSet(stream, param, value)`, `SvStreamParameterGet`, `SvStreamParameterHas`, `SvStreamParameterReset`.
* **Smooth Timed Fade (`time` in milliseconds):**
  * `SvStreamParameterSlideFromTo(stream, param, startValue, endValue, timeMs)`
  * `SvStreamParameterSlideTo(stream, param, endValue, timeMs)`
  * `SvStreamParameterSlide(stream, param, deltaValue, timeMs)`

Example — smooth 200 ms fade-out on megaphone toggle:
```pawn
SvStreamParameterSlideTo(g_PlazaStream, SV_PARAMETER_VOLUME, 0.0, 200);
```

---

## 3. DX8 Audio Effects

SampVoice Plus supports all **9 DirectX 8 / BASS audio effects** applied cleanly in real time on attached streams:

| Native Creator | Purpose / Typical Recipe |
| :--- | :--- |
| `SvEffectCreateChorus` | Rich vocal doubling / choir shimmer (`wetdrymix`, `depth`, `feedback`, `frequency`, `waveform`, `delay`, `phase`) |
| `SvEffectCreateCompressor` | Broadcast speech leveling (`gain`, `attack`, `release`, `threshold`, `ratio`, `predelay`) |
| `SvEffectCreateDistortion` | Gritty walkie-talkie / megaphone saturation (`gain`, `edge`, `posteqcenterfrequency`, `posteqbandwidth`, `prelowpasscutoff`) |
| `SvEffectCreateEcho` | Cave / stadium / PA speaker slapback (`wetdrymix`, `feedback`, `leftdelay`, `rightdelay`, `pandelay`) |
| `SvEffectCreateFlanger` | Metallic sweep modulation (`wetdrymix`, `depth`, `feedback`, `frequency`, `waveform`, `delay`, `phase`) |
| `SvEffectCreateGargle` | Tremolo / alien voice chopper (`ratehz`, `waveshape`) |
| `SvEffectCreateI3dl2reverb` | Full room acoustic preset engine (`room`, `roomhf`, `decaytime`, `diffusion`, `density`, etc.) |
| `SvEffectCreateParameq` | Narrow-band telephone or police radio voice filtering (`center`, `bandwidth`, `gain`) |
| `SvEffectCreateReverb` | Lightweight indoor/hall reverb (`ingain`, `reverbmix`, `reverbtime`, `highfreqrtratio`) |

Effects are reusable across multiple streams:
```pawn
new SV_EFFECT:fx = SvEffectCreateParameq(1, 1800.0, 12.0, -6.0);
SvEffectAttachStream(fx, g_RadioStream);
// Later on cleanup:
SvEffectDetachStream(fx, g_RadioStream);
SvEffectDelete(fx);
```

---

## 4. Stream Icons (`SvStreamSetIcon`)

Every stream defaults to the `"speaker"` icon. For **SampVoice Plus (`v12`)** clients, you can assign any of the 7 built-in vector-rendered HUD icons to visually distinguish radios, phone calls, megaphones, police dispatch, admin announcements, and global rooms—both in the on-screen Speaker List and floating overhead 3D icons:

| Icon Name | Meaning | Default? |
| :--- | :--- | :---: |
| `"speaker"` | General voice / proximity speaker | Yes |
| `"radio"` | Walkie-talkie / faction radio channel | — |
| `"phone"` | Private 1-on-1 phone call | — |
| `"megaphone"` | Public announcement / bullhorn | — |
| `"police"` | Law enforcement badge / dispatch | — |
| `"admin"` | Staff / administrator broadcast | — |
| `"global"` | Server-wide room / lobby chat | — |

> [!NOTE]
> * Pass lowercase icon names (`"radio"`, `"phone"`, etc.). Any unrecognized string logs a single warning server-side and falls back to `"speaker"`.
> * Icon updates (`PlusStreamIcon`) are only sent to `v12` clients and are synchronized automatically both on live change and when attaching a `v12` listener later. Legacy `v11` clients ignore this extension cleanly and keep the default speaker sprite.
> * If a remote player speaks simultaneously on multiple active streams, the client's Speaker List and overhead 3D icon display the stream that first appears for that speaker.

### Examples (`"radio"` and `"phone"`)
```pawn
// Faction walkie-talkie channel
new SV_GSTREAM:g_PdRadio = SvCreateGStream(0x3498DBFF, "LSPD Radio");
SvStreamSetIcon(g_PdRadio, "radio");

// Private phone call between caller and callee
new SV_GSTREAM:callStream = SvCreateGStream(0x2ECC71FF, "Phone Call");
SvStreamSetIcon(callStream, "phone");
SvAttachListenerToStream(callStream, callerid);
SvAttachListenerToStream(callStream, calleeid);
SvAttachSpeakerToStream(callStream, callerid);
SvAttachSpeakerToStream(callStream, calleeid);
```
