# WAV file creation helpers for STSPEECH
# 16-bit signed PCM, 22050 Hz, mono

import struct


SAMPLE_RATE = 22050
NUM_CHANNELS = 1
BITS_PER_SAMPLE = 16


def pcm_to_wav(pcm_data, sample_rate=SAMPLE_RATE):
    """Convert raw 16-bit signed PCM bytes to WAV file bytes.

    Args:
        pcm_data: Raw PCM data (16-bit signed, little-endian)
        sample_rate: Sample rate in Hz (default 22050)

    Returns:
        Complete WAV file as bytes
    """
    byte_rate = sample_rate * NUM_CHANNELS * BITS_PER_SAMPLE // 8
    block_align = NUM_CHANNELS * BITS_PER_SAMPLE // 8
    data_size = len(pcm_data)

    header = b'RIFF'
    header += struct.pack('<I', data_size + 36)
    header += b'WAVE'
    header += b'fmt '
    header += struct.pack('<I', 16)
    header += struct.pack('<H', 1)  # PCM format
    header += struct.pack('<H', NUM_CHANNELS)
    header += struct.pack('<I', sample_rate)
    header += struct.pack('<I', byte_rate)
    header += struct.pack('<H', block_align)
    header += struct.pack('<H', BITS_PER_SAMPLE)
    header += b'data'
    header += struct.pack('<I', data_size)

    return header + pcm_data


def save_wav(pcm_data, filename, sample_rate=SAMPLE_RATE):
    """Save raw PCM data as a WAV file.

    Args:
        pcm_data: Raw PCM data (16-bit signed, little-endian)
        filename: Output file path
        sample_rate: Sample rate in Hz (default 22050)
    """
    wav_data = pcm_to_wav(pcm_data, sample_rate)
    with open(filename, 'wb') as f:
        f.write(wav_data)
