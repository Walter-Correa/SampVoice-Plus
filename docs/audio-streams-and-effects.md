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

* **Distance Gain ($g_{\text{spatial}}$):** Given listener-to-source distance $d = \lVert \mathbf{p}_{\text{source}} - \mathbf{p}_{\text{cam}} \rVert$, $d_{\max} = \max(1.0, \text{distance})$, $d_{\min} = 0.12 \cdot d_{\max}$ (matching `BASS_3DMODE_NORMAL` with rolloff `1.0`), and a smoothstep fade $w(t) = t^2(3 - 2t)$ over the outer 15% ($t = \operatorname{clamp}(\frac{d_{\max} - d}{0.15 \cdot d_{\max}},\, 0,\, 1)$):
  $$g_{\text{spatial}}(d) = \begin{cases} 1.0 & d \le d_{\min} \\ \dfrac{d_{\min}}{d} & d_{\min} < d \le 0.85\,d_{\max} \\ \dfrac{d_{\min}}{d} \cdot w(t) & 0.85\,d_{\max} < d < d_{\max} \\ 0.0 & d \ge d_{\max} \end{cases}$$
* **Stereo Pan ($p_{\text{spatial}}$):** Computed from the camera right vector $\mathbf{r} = \widehat{\mathbf{f} \times \mathbf{t}}$ and unit direction $\hat{\mathbf{u}} = \frac{\mathbf{p}_{\text{source}} - \mathbf{p}_{\text{cam}}}{d}$ capped at $\pm 0.8$ and tapered near the camera ($d < d_{\min}$):
  $$p_{\text{spatial}} = 0.8 \cdot \operatorname{clamp}(\hat{\mathbf{u}} \cdot \mathbf{r},\, -1.0,\, +1.0) \cdot \operatorname{clamp}\!\left(\frac{d}{d_{\min}},\, 0.0,\, 1.0\right)$$
  Applied via `BASS_ATTRIB_PAN` with `50 ms` `BASS_ChannelSlideAttribute` smoothing between ticks.
* **Script Parameter Composition:** `SV_PARAMETER_VOLUME` multiplies ($g_{\text{final}} = \text{masterVol} \cdot g_{\text{stream}} \cdot g_{\text{spatial}}$) and `SV_PARAMETER_PANNING` adds ($p_{\text{final}} = \operatorname{clamp}(p_{\text{spatial}} + p_{\text{script}},\, -1.0,\, +1.0)$). **Global streams** use $g_{\text{spatial}} = 1.0$ and $p_{\text{spatial}} = 0.0$.

---

## 2. Dynamic Stream Parameters & Sliding

Each stream exposes 5 controllable `SV_PARAMETER` attributes:

| Enum Constant | Value | Meaning | Typical Default |
| :--- | :---: | :--- | :---: |
| `SV_PARAMETER_FREQUENCY` | `1` | Sample playback rate / pitch (Hz) | `48000.0` |
| `SV_PARAMETER_VOLUME` | `2` | Gain multiplier (`0.0` .. `1.0+`), combined with software spatial distance attenuation | `1.0` |
| `SV_PARAMETER_PANNING` | `3` | Stereo pan offset (`-1.0` left .. `1.0` right), added onto software spatial stereo pan | `0.0` |
| `SV_PARAMETER_EAXMIX` | `4` | Legacy script compatibility no-op (accepted cleanly, no audible effect; hardware EAX/BASS 3D is omitted to keep SA-MP audio intact) | `0.0` |
| `SV_PARAMETER_SRC` | `8` | BASS sample interpolation quality | `8.0` |

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
