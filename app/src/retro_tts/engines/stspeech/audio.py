# audio.py - PCM waveform generation via YM2149 chip emulation
# Ported from engine/src/audio.c + ym2149.c
#
# Frame-by-frame outer loop. If numpy is available, uses vectorized inner
# loops; otherwise falls back to pure Python.

import struct
import sys
from array import array

try:
    import numpy as np
    _HAS_NP = True
except ImportError:
    _HAS_NP = False

from .synth_tables import (
    wave_tables, snd_regs,
    FRAME_SIZE, FR_SYNTH_CNT, FR_VOICE_CTRL, FR_NOISE_PERIOD,
    FR_F1_FREQ, FR_F1_AMP, FR_F2_FREQ, FR_F2_AMP,
    FR_F3_FREQ, FR_F3_AMP,
)

SAMPLE_RATE = 22050
OUTPUT_SCALE = 150.0

# Byte offsets that are linearly interpolated between frames
_INTERP_OFFSETS = (FR_SYNTH_CNT, FR_F1_FREQ, FR_F1_AMP,
                   FR_F2_FREQ, FR_F2_AMP, FR_F3_FREQ, FR_F3_AMP)
# Byte offsets that use step function (keep current frame's value)
_STEP_OFFSETS = (FR_VOICE_CTRL, FR_NOISE_PERIOD)


def _upsample_frames(frame_data, num_frames, factor):
    """Upsample frame buffer by inserting interpolated subframes.

    For each consecutive pair of original frames, generates *factor*
    subframes with linearly interpolated parameters.  Voice control
    and noise period are kept as step functions (no interpolation).

    With factor=1 the input is returned unchanged (no-op path).

    Returns (new_frame_data, new_num_frames).
    """
    if factor <= 1:
        return frame_data, num_frames

    new_num_frames = num_frames * factor
    new_data = bytearray(new_num_frames * FRAME_SIZE)

    for i in range(num_frames):
        cur_base = i * FRAME_SIZE
        # For the last frame there is no "next", so we interpolate toward itself
        next_i = min(i + 1, num_frames - 1)
        next_base = next_i * FRAME_SIZE

        for sub in range(factor):
            t = sub / factor  # 0.0 .. (factor-1)/factor
            out_base = (i * factor + sub) * FRAME_SIZE

            # Interpolated bytes
            for off in _INTERP_OFFSETS:
                cur_val = frame_data[cur_base + off]
                nxt_val = frame_data[next_base + off]
                new_data[out_base + off] = int(round(
                    cur_val * (1.0 - t) + nxt_val * t))

            # Step bytes (keep current frame value)
            for off in _STEP_OFFSETS:
                new_data[out_base + off] = frame_data[cur_base + off]

    return bytes(new_data), new_num_frames

# YM2149 logarithmic DAC (~1.5 dB per step)
_ym_dac = [0, 1, 1, 2, 3, 4, 6, 8, 11, 16, 22, 31, 44, 63, 89, 127]

# The register bytes for a waveform sum never change, and the renderer only
# reads three of them through the DAC table, so the whole per-tick lookup is
# a table of its own.  Two of them: the unvoiced path adds the noise channel
# in place of the third.
_REG_ABC = tuple(
    _ym_dac[reg[1] & 0x0F] + _ym_dac[reg[3] & 0x0F] + _ym_dac[reg[5] & 0x0F]
    for reg in snd_regs
)
_REG_AB = tuple(
    _ym_dac[reg[1] & 0x0F] + _ym_dac[reg[3] & 0x0F]
    for reg in snd_regs
)


def _precompute_lfsr():
    """Pre-compute the full 17-bit LFSR noise cycle (131071 bits)."""
    cycle_len = (1 << 17) - 1
    bits = bytearray(cycle_len)
    lfsr = 1
    for i in range(cycle_len):
        bit = lfsr & 1
        bits[i] = bit
        lfsr >>= 1
        if bit:
            lfsr ^= 0x12000
    return bits

_lfsr_bits = _precompute_lfsr()
_lfsr_len = len(_lfsr_bits)


def _compute_noise_sequence(nticks, noise_period, lfsr_pos,
                            noise_counter, ym_ticks_per_isr):
    """Compute noise output bits for each ISR tick.

    Returns (noise_bits list, final lfsr_pos, final noise_counter).
    """
    noise_bits = [0] * nticks
    np_val = max(noise_period, 1)
    ym_int = int(ym_ticks_per_isr)
    nc = noise_counter
    lp = lfsr_pos
    current_noise = int(_lfsr_bits[(lp - 1) % _lfsr_len]) if lp > 0 else 0

    for t in range(nticks):
        nc += ym_int
        advances = nc // np_val
        nc = nc % np_val
        if advances > 0:
            lp += advances
            current_noise = int(_lfsr_bits[(lp - 1) % _lfsr_len])
        noise_bits[t] = current_noise

    return noise_bits, lp, nc


# ---------------------------------------------------------------------------
# Numpy-accelerated path
# ---------------------------------------------------------------------------
def _render_numpy(spchbuff, num_frames, rate, pitch, ticks_per_frame, orig_tpf):
    """Render with numpy vectorization. Returns bytes (int16 LE)."""
    frames = np.frombuffer(spchbuff, dtype=np.uint8).reshape(num_frames, FRAME_SIZE)

    wt_np = np.array(wave_tables, dtype=np.int8)
    sr_np = np.array(snd_regs, dtype=np.uint8)
    dac_np = np.array(_ym_dac, dtype=np.float64)

    isr_rate = 2457600.0 / (4.0 * pitch)
    total_ticks = num_frames * ticks_per_frame
    num_pcm = max(1, int(total_ticks * SAMPLE_RATE / isr_rate) + 1)
    ym_ticks_per_isr = 2000000.0 / isr_rate
    tick_step = isr_rate / SAMPLE_RATE

    all_tick_outputs = np.zeros(total_ticks, dtype=np.float64)

    phase_a = phase_b = phase_c = 0
    synth_done = 0
    lfsr_pos = 0
    noise_counter = 0
    tick_offset = 0

    for fi in range(num_frames):
        fp = frames[fi]
        synth_cnt = int(fp[FR_SYNTH_CNT]) or 0x42
        voice_ctrl = int(fp[FR_VOICE_CTRL])
        noise_period = int(fp[FR_NOISE_PERIOD]) or 1
        freq_a_val = int(fp[FR_F1_FREQ])
        amp_a = int(fp[FR_F1_AMP]) & 0x0F
        freq_b_val = int(fp[FR_F2_FREQ])
        amp_b = int(fp[FR_F2_AMP]) & 0x0F
        freq_c_val = int(fp[FR_F3_FREQ])
        amp_c = int(fp[FR_F3_AMP]) & 0x0F

        nticks = ticks_per_frame
        is_voiced = (voice_ctrl == 0)

        if is_voiced:
            pa_arr = np.empty(nticks, dtype=np.uint8)
            pb_arr = np.empty(nticks, dtype=np.uint8)
            pc_arr = np.empty(nticks, dtype=np.uint8)
            pa, pb, pc = phase_a, phase_b, phase_c
            sd = synth_done

            for t in range(nticks):
                sd -= 1
                if sd < 0:
                    sd = synth_cnt
                    pa = pb = pc = 0
                pa = (pa + freq_a_val) & 0xFF
                pb = (pb + freq_b_val) & 0xFF
                pc = (pc + freq_c_val) & 0xFF
                pa_arr[t] = pa
                pb_arr[t] = pb
                pc_arr[t] = pc

            phase_a, phase_b, phase_c = pa, pb, pc
            synth_done = sd
        else:
            ticks_idx = np.arange(1, nticks + 1, dtype=np.int32)
            pa_arr = ((phase_a + ticks_idx * freq_a_val) & 0xFF).astype(np.uint8)
            pb_arr = ((phase_b + ticks_idx * freq_b_val) & 0xFF).astype(np.uint8)
            pc_arr = ((phase_c + ticks_idx * freq_c_val) & 0xFF).astype(np.uint8)
            if nticks > 0:
                phase_a = int(pa_arr[-1])
                phase_b = int(pb_arr[-1])
                phase_c = int(pc_arr[-1])

        val_a = wt_np[amp_a, pa_arr].view(np.uint8)
        val_b = wt_np[amp_b, pb_arr].view(np.uint8)
        val_c = wt_np[amp_c, pc_arr].view(np.uint8)
        wsum = (val_a.astype(np.uint16) + val_b.astype(np.uint16) +
                val_c.astype(np.uint16)).astype(np.uint8)

        regs = sr_np[wsum]
        vol_a_arr = regs[:, 1] & 0x0F
        vol_b_arr = regs[:, 3] & 0x0F

        if is_voiced:
            vol_c_arr = regs[:, 5] & 0x0F
            output = (dac_np[vol_a_arr] + dac_np[vol_b_arr] +
                      dac_np[vol_c_arr])
        else:
            noise_bits, lfsr_pos, noise_counter = _compute_noise_sequence(
                nticks, noise_period, lfsr_pos, noise_counter, ym_ticks_per_isr)
            noise_np = np.array(noise_bits, dtype=np.float64)
            chc_vol = voice_ctrl & 0x0F
            output = (dac_np[vol_a_arr] + dac_np[vol_b_arr] +
                      noise_np * dac_np[chc_vol])

        end_tick = min(tick_offset + nticks, total_ticks)
        n = end_tick - tick_offset
        all_tick_outputs[tick_offset:end_tick] = output[:n]
        tick_offset += nticks

    # Resample ISR ticks -> 22050 Hz PCM (linear interpolation)
    pcm_indices = np.arange(num_pcm, dtype=np.float64) * tick_step
    pcm_indices = np.clip(pcm_indices, 0, total_ticks - 1)

    idx_low = pcm_indices.astype(np.int64)
    idx_high = np.minimum(idx_low + 1, total_ticks - 1)
    frac = pcm_indices - idx_low
    pcm_float = (all_tick_outputs[idx_low] * (1.0 - frac) +
                 all_tick_outputs[idx_high] * frac)

    # Low-pass filter (~8 kHz cutoff)
    dt = 1.0 / SAMPLE_RATE
    rc = 1.0 / (6.2832 * 8000.0)
    alpha = dt / (rc + dt)

    filtered = np.empty(num_pcm, dtype=np.float64)
    prev = 0.0
    for i in range(num_pcm):
        prev += alpha * (pcm_float[i] - prev)
        filtered[i] = prev

    scaled = filtered * OUTPUT_SCALE
    pcm = np.clip(scaled, -32768, 32767).astype(np.int16)

    dc_offset = int(np.mean(pcm))
    pcm = (pcm.astype(np.int32) - dc_offset).clip(-32768, 32767).astype(np.int16)

    # Rate-adaptive smoothstep fade-out covering ~2.5 frames of PCM
    isr_rate_val = 2457600.0 / (4.0 * pitch)
    pcm_per_frame = orig_tpf * SAMPLE_RATE / isr_rate_val
    fade_out_samples = min(int(pcm_per_frame * 2.5), len(pcm) // 3)
    fade_out_samples = max(fade_out_samples, 132)

    # Fade-in: short linear ramp (start-of-speech is fine)
    fade_in_samples = min(132, len(pcm) // 4)
    if fade_in_samples > 0:
        ramp = np.linspace(0.0, 1.0, fade_in_samples)
        pcm[:fade_in_samples] = (pcm[:fade_in_samples].astype(np.float64) * ramp).astype(np.int16)

    # Fade-out: smoothstep (zero slope at endpoints → no knee artifacts)
    if fade_out_samples > 0:
        t = np.linspace(0.0, 1.0, fade_out_samples)
        smooth = 1.0 - (3.0 * t * t - 2.0 * t * t * t)
        pcm[-fade_out_samples:] = (pcm[-fade_out_samples:].astype(np.float64) * smooth).astype(np.int16)

    return pcm.tobytes()


# ---------------------------------------------------------------------------
# Pure Python fallback path
# ---------------------------------------------------------------------------
def _render_python(spchbuff, num_frames, rate, pitch, ticks_per_frame, orig_tpf):
    """Render with pure Python loops. Returns bytes (int16 LE)."""
    isr_rate = 2457600.0 / (4.0 * pitch)
    total_ticks = num_frames * ticks_per_frame
    num_pcm = max(1, int(total_ticks * SAMPLE_RATE / isr_rate) + 1)
    ym_ticks_per_isr = 2000000.0 / isr_rate
    tick_step = isr_rate / SAMPLE_RATE

    all_tick_outputs = [0.0] * total_ticks

    phase_a = phase_b = phase_c = 0
    synth_done = 0
    lfsr_pos = 0
    noise_counter = 0
    tick_offset = 0

    for fi in range(num_frames):
        base = fi * FRAME_SIZE
        synth_cnt = spchbuff[base + FR_SYNTH_CNT] or 0x42
        voice_ctrl = spchbuff[base + FR_VOICE_CTRL]
        noise_period = spchbuff[base + FR_NOISE_PERIOD] or 1
        freq_a_val = spchbuff[base + FR_F1_FREQ]
        amp_a = spchbuff[base + FR_F1_AMP] & 0x0F
        freq_b_val = spchbuff[base + FR_F2_FREQ]
        amp_b = spchbuff[base + FR_F2_AMP] & 0x0F
        freq_c_val = spchbuff[base + FR_F3_FREQ]
        amp_c = spchbuff[base + FR_F3_AMP] & 0x0F

        nticks = ticks_per_frame
        is_voiced = (voice_ctrl == 0)

        wt_a = wave_tables[amp_a]
        wt_b = wave_tables[amp_b]
        wt_c = wave_tables[amp_c]

        if is_voiced:
            pa, pb, pc = phase_a, phase_b, phase_c
            sd = synth_done

            for t in range(nticks):
                sd -= 1
                if sd < 0:
                    sd = synth_cnt
                    pa = pb = pc = 0
                pa = (pa + freq_a_val) & 0xFF
                pb = (pb + freq_b_val) & 0xFF
                pc = (pc + freq_c_val) & 0xFF

                wsum_val = (wt_a[pa] + wt_b[pb] + wt_c[pc]) & 0xFF
                out = _REG_ABC[wsum_val]

                idx = tick_offset + t
                if idx < total_ticks:
                    all_tick_outputs[idx] = float(out)

            phase_a, phase_b, phase_c = pa, pb, pc
            synth_done = sd
        else:
            # Unvoiced: free-running phases + noise gating on channel C
            noise_bits, lfsr_pos, noise_counter = _compute_noise_sequence(
                nticks, noise_period, lfsr_pos, noise_counter, ym_ticks_per_isr)
            chc_vol = voice_ctrl & 0x0F
            dac_chc = _ym_dac[chc_vol]

            pa, pb, pc = phase_a, phase_b, phase_c
            for t in range(nticks):
                pa = (pa + freq_a_val) & 0xFF
                pb = (pb + freq_b_val) & 0xFF
                pc = (pc + freq_c_val) & 0xFF

                wsum_val = (wt_a[pa] + wt_b[pb] + wt_c[pc]) & 0xFF
                out = _REG_AB[wsum_val] + noise_bits[t] * dac_chc

                idx = tick_offset + t
                if idx < total_ticks:
                    all_tick_outputs[idx] = float(out)

            phase_a, phase_b, phase_c = pa, pb, pc

        tick_offset += nticks

    # Resample ISR ticks -> 22050 Hz PCM (linear interpolation)
    pcm_float = [0.0] * num_pcm
    max_idx = total_ticks - 1
    for i in range(num_pcm):
        pos = i * tick_step
        if pos < 0.0:
            pos = 0.0
        elif pos > max_idx:
            pos = max_idx
        il = int(pos)
        ih = il + 1 if il < max_idx else il
        f = pos - il
        pcm_float[i] = all_tick_outputs[il] * (1.0 - f) + all_tick_outputs[ih] * f

    # Low-pass filter (~8 kHz cutoff)
    dt = 1.0 / SAMPLE_RATE
    rc = 1.0 / (6.2832 * 8000.0)
    alpha = dt / (rc + dt)

    prev = 0.0
    for i in range(num_pcm):
        prev += alpha * (pcm_float[i] - prev)
        pcm_float[i] = prev

    # Scale, clip, remove DC
    pcm_float = [value * OUTPUT_SCALE for value in pcm_float]

    dc = sum(pcm_float) / len(pcm_float) if pcm_float else 0.0

    # Rate-adaptive fade lengths
    isr_rate_val = 2457600.0 / (4.0 * pitch)
    pcm_per_frame = orig_tpf * SAMPLE_RATE / isr_rate_val
    fade_out_samples = min(int(pcm_per_frame * 2.5), num_pcm // 3)
    fade_out_samples = max(fade_out_samples, 132)
    fade_in_samples = min(132, num_pcm // 4)
    fade_out_start = num_pcm - fade_out_samples

    samples = array("h", bytes(num_pcm * 2))
    for i in range(num_pcm):
        v = int(pcm_float[i] - dc)
        if v < -32768:
            v = -32768
        elif v > 32767:
            v = 32767
        # Apply fade ramps
        if fade_in_samples > 0 and i < fade_in_samples:
            v = int(v * i / fade_in_samples)
        elif fade_out_samples > 0 and i >= fade_out_start:
            t = (i - fade_out_start) / fade_out_samples
            smooth = 1.0 - (3.0 * t * t - 2.0 * t * t * t)
            v = int(v * smooth)
        samples[i] = v

    if sys.byteorder != "little":
        samples.byteswap()
    return samples.tobytes()


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------
def audio_render_pcm(spchbuff, num_frames, rate, pitch, subframe_factor=1):
    """Render speech parameter frames to 16-bit PCM audio at 22050 Hz.

    Args:
        spchbuff: Frame data as bytes (num_frames * FRAME_SIZE).
        num_frames: Number of frames.
        rate: Speech rate.
        pitch: Speech pitch.
        subframe_factor: Upsampling factor (1=original, 2=2x, 4=4x).

    Returns:
        PCM audio as bytes (16-bit signed LE, 22050 Hz), or None on error.
    """
    if not spchbuff or num_frames <= 0 or pitch <= 0:
        return None

    spchbuff, num_frames = _upsample_frames(spchbuff, num_frames,
                                             subframe_factor)
    orig_tpf = max(1, (rate * 0x4D) // pitch)
    ticks_per_frame = max(1, orig_tpf // subframe_factor)

    if _HAS_NP:
        return _render_numpy(spchbuff, num_frames, rate, pitch,
                             ticks_per_frame, orig_tpf)
    else:
        return _render_python(spchbuff, num_frames, rate, pitch,
                              ticks_per_frame, orig_tpf)
