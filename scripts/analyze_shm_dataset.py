#!/usr/bin/env python3
"""
Ronin SHM Binary Dataset Analyzer (PhoneSHM 20-Byte Binary Format)
------------------------------------------------------------------
Binary Format per sample (20 bytes total):
  - [0..3]   Optional Magic Header 'SHM1' (first 4 bytes of file)
  - [0..7]   Timestamp: 8 bytes int64 (nanoseconds) or float64
  - [8..11]  Acc X:     4 bytes float32 (m/s^2)
  - [12..15] Acc Y:     4 bytes float32 (m/s^2)
  - [16..19] Acc Z:     4 bytes float32 (m/s^2)

Accompanied by .meta.json files with structural and sensor metadata.
"""

import os
import sys
import glob
import json
import struct
import argparse
import numpy as np
from scipy import signal

def parse_shm_bin_file(bin_path):
    """
    Parses a single PhoneSHM .bin file into numpy arrays.
    Returns (timestamps_ns, x, y, z, sample_rate_hz).
    """
    file_size = os.path.getsize(bin_path)
    if file_size < 24:
        raise ValueError(f"File {bin_path} is too small ({file_size} bytes)")

    with open(bin_path, "rb") as f:
        header = f.read(4)
        if header == b"SHM1":
            offset = 4
        else:
            offset = 0
            f.seek(0)
        
        num_samples = (file_size - offset) // 20
        raw_bytes = f.read(num_samples * 20)

    dtype = np.dtype([("ts", "<i8"), ("x", "<f4"), ("y", "<f4"), ("z", "<f4")])
    arr = np.frombuffer(raw_bytes, dtype=dtype)
    
    # Calculate sample rate from timestamps
    ts = arr["ts"]
    if len(ts) > 1:
        dt_sec = np.median(np.diff(ts)) * 1e-9
        calc_fs = 1.0 / dt_sec if dt_sec > 0 else 100.0
    else:
        calc_fs = 100.0

    return ts, arr["x"], arr["y"], arr["z"], calc_fs

def analyze_vibration(x, y, z, fs, high_pass_cutoff=0.5):
    """
    Performs comprehensive structural vibration analysis on 3-axis accelerometer data.
    """
    num_samples = len(x)
    duration_s = num_samples / fs

    # 1. Static DC Components & Tilt
    mean_x = float(np.mean(x))
    mean_y = float(np.mean(y))
    mean_z = float(np.mean(z))
    total_g = np.sqrt(mean_x**2 + mean_y**2 + mean_z**2)
    
    # Tilt angle from vertical (Z-axis) in degrees
    tilt_deg = float(np.degrees(np.arccos(np.clip(abs(mean_z) / (total_g + 1e-9), 0.0, 1.0))))

    # 2. Dynamic Component (Zero-mean vibration)
    # Apply 2nd order Butterworth high-pass filter to remove slow sensor drift
    sos = signal.butter(2, high_pass_cutoff, btype='highpass', fs=fs, output='sos')
    x_dyn = signal.sosfilt(sos, x - mean_x)
    y_dyn = signal.sosfilt(sos, y - mean_y)
    z_dyn = signal.sosfilt(sos, z - mean_z)
    
    # Combined horizontal and total dynamic magnitude
    h_dyn = np.sqrt(x_dyn**2 + y_dyn**2)
    mag_dyn = np.sqrt(x_dyn**2 + y_dyn**2 + z_dyn**2)

    # Statistical Vibration Metrics (in mg: 1 mg = 0.00981 m/s^2)
    to_mg = 1000.0 / 9.80665
    rms_x_mg = float(np.std(x_dyn) * to_mg)
    rms_y_mg = float(np.std(y_dyn) * to_mg)
    rms_z_mg = float(np.std(z_dyn) * to_mg)
    rms_h_mg = float(np.sqrt(np.mean(h_dyn**2)) * to_mg)
    rms_total_mg = float(np.sqrt(np.mean(mag_dyn**2)) * to_mg)
    
    peak_acc_mg = float(np.max(mag_dyn) * to_mg)
    crest_factor = float(peak_acc_mg / (rms_total_mg + 1e-6))

    # 3. Welch Power Spectral Density (PSD)
    nperseg = min(2048, num_samples)
    noverlap = nperseg // 2
    freqs, psd_mag = signal.welch(mag_dyn, fs=fs, window='hann', nperseg=nperseg, noverlap=noverlap)
    _, psd_x = signal.welch(x_dyn, fs=fs, window='hann', nperseg=nperseg, noverlap=noverlap)
    _, psd_y = signal.welch(y_dyn, fs=fs, window='hann', nperseg=nperseg, noverlap=noverlap)
    _, psd_z = signal.welch(z_dyn, fs=fs, window='hann', nperseg=nperseg, noverlap=noverlap)

    # Convert PSD to dB (ref: 1 (m/s^2)^2/Hz)
    psd_db = 10.0 * np.log10(psd_mag + 1e-15)
    noise_floor_db = float(np.percentile(psd_db, 20))

    # 4. Resonant Modal Peak Detection (Exclude < 0.5 Hz sub-structural noise)
    valid_idx = np.where(freqs >= high_pass_cutoff)[0]
    sub_freqs = freqs[valid_idx]
    sub_psd_db = psd_db[valid_idx]
    
    peak_indices, properties = signal.find_peaks(sub_psd_db, prominence=3.0, distance=5)
    
    top_peaks = []
    if len(peak_indices) > 0:
        sorted_p = sorted(peak_indices, key=lambda idx: sub_psd_db[idx], reverse=True)[:5]
        for p_idx in sorted_p:
            f_val = float(sub_freqs[p_idx])
            p_val = float(sub_psd_db[p_idx])
            prom = float(properties["prominences"][np.where(peak_indices == p_idx)[0][0]]) if "prominences" in properties else 0.0
            
            # Half-power (-3dB) bandwidth and Q-Factor
            peak_linear = 10.0 ** (p_val / 10.0)
            half_power = peak_linear / 2.0
            
            # Find -3dB crossing points
            left_idx = p_idx
            while left_idx > 0 and 10.0 ** (sub_psd_db[left_idx] / 10.0) > half_power:
                left_idx -= 1
            right_idx = p_idx
            while right_idx < len(sub_psd_db) - 1 and 10.0 ** (sub_psd_db[right_idx] / 10.0) > half_power:
                right_idx += 1
                
            delta_f = max(0.05, float(sub_freqs[right_idx] - sub_freqs[left_idx]))
            q_factor = float(f_val / delta_f)
            damping_ratio = float(1.0 / (2.0 * q_factor) * 100.0) # in %

            top_peaks.append({
                "frequency_hz": round(f_val, 2),
                "psd_db": round(p_val, 2),
                "prominence_db": round(prom, 2),
                "q_factor": round(q_factor, 1),
                "damping_ratio_pct": round(damping_ratio, 2)
            })

    fundamental_f0 = top_peaks[0]["frequency_hz"] if top_peaks else 0.0
    f0_psd = top_peaks[0]["psd_db"] if top_peaks else -100.0
    snr_db = round(f0_psd - noise_floor_db, 2)

    # 5. Structural Health Index Estimation (%)
    # Wood / Timber 1-story resonance is typically 3 - 10 Hz with low ambient micro-vibration (< 15 mg)
    health_index = 100.0
    if rms_total_mg > 30.0:
        health_index -= min(30.0, (rms_total_mg - 30.0) * 0.5)
    if tilt_deg > 5.0:
        health_index -= min(20.0, (tilt_deg - 5.0) * 2.0)
    if snr_db < 6.0:
        health_index -= 5.0
    health_index = round(max(50.0, min(100.0, health_index)), 1)
    
    status = "HEALTHY" if health_index >= 90.0 else ("WARNING" if health_index >= 75.0 else "INSPECT")

    return {
        "num_samples": num_samples,
        "duration_seconds": round(duration_s, 2),
        "sample_rate_hz": round(fs, 2),
        "tilt_deg": round(tilt_deg, 2),
        "rms_total_mg": round(rms_total_mg, 3),
        "rms_horizontal_mg": round(rms_h_mg, 3),
        "rms_vertical_mg": round(rms_z_mg, 3),
        "peak_acc_mg": round(peak_acc_mg, 3),
        "crest_factor": round(crest_factor, 2),
        "noise_floor_db": round(noise_floor_db, 2),
        "snr_db": snr_db,
        "fundamental_frequency_hz": fundamental_f0,
        "health_index_pct": health_index,
        "status": status,
        "top_spectral_peaks": top_peaks
    }

def print_single_report(bin_path, res, meta):
    basename = os.path.basename(bin_path)
    b_type = meta.get("metadata", {}).get("buildingType", "N/A")
    b_name = meta.get("metadata", {}).get("buildingDisplayName", "N/A")
    floors = meta.get("metadata", {}).get("floors", "N/A")
    placement = meta.get("metadata", {}).get("phonePlacement", "N/A")
    device = meta.get("deviceReport", {}).get("deviceModel", "N/A")

    print("\n" + "=" * 70)
    print(f"  RONIN SHM ACCELEROMETER ANALYSIS REPORT: {basename}")
    print("=" * 70)
    print(f"  Building:     {b_name} ({b_type}, {floors} Floor)")
    print(f"  Placement:    {placement} | Device: {device}")
    print(f"  Duration:     {res['duration_seconds']}s ({res['duration_seconds']/60:.1f} mins) | Samples: {res['num_samples']}")
    print(f"  Sample Rate:  {res['sample_rate_hz']} Hz | Tilt Angle: {res['tilt_deg']}°")
    print("-" * 70)
    print("  [VIBRATION AMPLITUDES (Micro-tremor)]")
    print(f"  • Total RMS:        {res['rms_total_mg']} mg")
    print(f"  • Horizontal RMS:   {res['rms_horizontal_mg']} mg  (X/Y Shearing Energy)")
    print(f"  • Vertical RMS:     {res['rms_vertical_mg']} mg  (Z Axial Energy)")
    print(f"  • Peak Acc:         {res['peak_acc_mg']} mg | Crest Factor: {res['crest_factor']}")
    print("-" * 70)
    print("  [SPECTRAL & MODAL RESONANCE (Welch PSD)]")
    print(f"  • Fundamental (f₀): {res['fundamental_frequency_hz']} Hz")
    print(f"  • SNR:              {res['snr_db']} dB | Noise Floor: {res['noise_floor_db']} dB")
    print("  • Dominant Resonant Peaks:")
    for i, p in enumerate(res["top_spectral_peaks"], 1):
        print(f"    {i}. {p['frequency_hz']} Hz | PSD: {p['psd_db']} dB | Prominence: {p['prominence_db']} dB | Q-Factor: {p['q_factor']} (Damping: {p['damping_ratio_pct']}%)")
    print("-" * 70)
    status_icon = "🟢" if res['status'] == "HEALTHY" else ("🟡" if res['status'] == "WARNING" else "🔴")
    print(f"  [DIAGNOSIS] Health Index: {res['health_index_pct']}% | Status: {status_icon} {res['status']}")
    print("=" * 70 + "\n")

def main():
    parser = argparse.ArgumentParser(description="Ronin PhoneSHM 20-Byte Binary Accelerometer Analyzer")
    parser.add_argument("--dir", default="/data/data/com.termux/files/home/storage/downloads/phoneshm", help="Directory containing .bin files")
    parser.add_argument("--file", help="Specific .bin file to analyze")
    parser.add_argument("--export-json", help="Path to export structured analysis results")
    args = parser.parse_args()

    if args.file:
        bin_files = [args.file]
    else:
        bin_files = sorted(glob.glob(os.path.join(args.dir, "*.bin")))

    if not bin_files:
        print(f"No .bin files found in {args.dir}")
        return

    print(f"Found {len(bin_files)} PhoneSHM binary file(s). Running DSP analysis...")

    all_results = []
    for bin_p in bin_files:
        meta_p = bin_p.replace(".bin", ".meta.json")
        meta = {}
        if os.path.exists(meta_p):
            with open(meta_p) as mf:
                meta = json.load(mf)
        
        try:
            ts, x, y, z, fs = parse_shm_bin_file(bin_p)
            res = analyze_vibration(x, y, z, fs)
            res["filename"] = os.path.basename(bin_p)
            res["metadata"] = meta
            all_results.append(res)
            
            if len(bin_files) == 1:
                print_single_report(bin_p, res, meta)
        except Exception as e:
            print(f"Error analyzing {bin_p}: {e}")

    if len(bin_files) > 1:
        # Print summary table
        print("\n" + "=" * 95)
        print(f"{'Filename':<38} | {'Duration':<8} | {'f0 (Hz)':<8} | {'RMS (mg)':<9} | {'Health %':<9} | {'Status'}")
        print("-" * 95)
        for r in all_results:
            icon = "🟢" if r['status'] == "HEALTHY" else "🟡"
            print(f"{r['filename']:<38} | {r['duration_seconds']:>6.0f}s | {r['fundamental_frequency_hz']:>7.2f} | {r['rms_total_mg']:>8.3f} | {r['health_index_pct']:>7.1f}% | {icon} {r['status']}")
        print("=" * 95)
        
        # Summary statistics
        f0_list = [r['fundamental_frequency_hz'] for r in all_results if r['fundamental_frequency_hz'] > 0]
        rms_list = [r['rms_total_mg'] for r in all_results]
        health_list = [r['health_index_pct'] for r in all_results]
        
        print("\n[DATASET SUMMARY]")
        print(f"  Total Valid Recordings: {len(all_results)} sessions")
        print(f"  Average Fundamental Frequency (f0): {np.mean(f0_list):.2f} Hz (Std: {np.std(f0_list):.2f} Hz)")
        print(f"  Average Ambient Vibration Energy:   {np.mean(rms_list):.3f} mg (Min: {np.min(rms_list):.3f}, Max: {np.max(rms_list):.3f})")
        print(f"  Average Structural Health Index:    {np.mean(health_list):.1f}%")
        print(f"  Healthy: {sum(1 for r in all_results if r['status'] == 'HEALTHY')} | Warning: {sum(1 for r in all_results if r['status'] == 'WARNING')}\n")

    if args.export_json:
        with open(args.export_json, "w") as jf:
            json.dump(all_results, jf, indent=2)
        print(f"Analysis exported to {args.export_json}")

if __name__ == "__main__":
    main()
