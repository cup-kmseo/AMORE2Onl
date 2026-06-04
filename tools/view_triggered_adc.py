#!/usr/bin/env python3
"""
view_triggered_adc.py -- triggered-channel ADC amplitude distribution viewer

각 crystal 에 기록된 이벤트(= 트리거된 채널)에 대해
phonon / photon 의 진폭 (peak - baseline) 분포를 ADC 단위로 플롯한다.

  baseline = waveform 앞 nbase 샘플의 평균
  amplitude = max(waveform) - baseline       (양의 신호 기준)
              또는 baseline - min(waveform)  (--neg 플래그 사용 시)

Usage:
    view_triggered_adc.py <file.h5> [options]

Options:
    --nbase N         baseline 계산에 쓸 앞쪽 샘플 수 (default: 100)
    --nch-per-adc N   ADC 당 crystal 수 (default: 8)
    --neg             음의 신호(baseline - min) 방향으로 진폭 계산
    --bins N          히스토그램 bin 수 (default: 80)
    --save DIR        대화형 표시 대신 DIR 에 PNG 저장
    --batch N         한 번에 읽을 crystal 행 수 (default: 500)
"""

import sys
import argparse
import os
import numpy as np
import matplotlib
import matplotlib.pyplot as plt
import h5py


def collect_amplitudes(fname, nbase, batch_size, negative):
    """
    HDF5 파일에서 crystal 별 phonon/photon 진폭 목록을 수집.

    Returns
    -------
    amp_phonon : dict  {crystal_id: np.ndarray of amplitude values}
    amp_photon : dict
    """
    amp_phonon = {}
    amp_photon = {}

    with h5py.File(fname, "r") as f:
        chs_ds    = f["events/chs"]
        phonon_ds = f["events/phonon"]
        photon_ds = f["events/photon"]

        total_crystals = phonon_ds.shape[0]
        ndp            = phonon_ds.shape[1]
        nb             = min(nbase, ndp)

        print(f"file           : {fname}")
        print(f"total crystals : {total_crystals}  (across all events)")
        print(f"ndp            : {ndp}")
        print(f"nbase (baseline): {nb}")
        direction = "baseline - min  (negative signal)" if negative else "max - baseline  (positive signal)"
        print(f"amplitude      : {direction}")
        print(f"reading in batches of {batch_size} crystals ...", flush=True)

        all_ids = chs_ds["id"][:].astype(np.int32)

        for start in range(0, total_crystals, batch_size):
            end = min(start + batch_size, total_crystals)

            pn_batch = phonon_ds[start:end, :].astype(np.float32)  # (batch, ndp)
            pt_batch = photon_ds[start:end, :].astype(np.float32)

            pn_base = pn_batch[:, :nb].mean(axis=1)   # (batch,)
            pt_base = pt_batch[:, :nb].mean(axis=1)

            if negative:
                pn_amp = pn_base - pn_batch.min(axis=1)
                pt_amp = pt_base - pt_batch.min(axis=1)
            else:
                pn_amp = pn_batch.max(axis=1) - pn_base
                pt_amp = pt_batch.max(axis=1) - pt_base

            ids_batch = all_ids[start:end]

            for j, cid in enumerate(ids_batch):
                cid = int(cid)
                if cid not in amp_phonon:
                    amp_phonon[cid] = []
                    amp_photon[cid] = []
                amp_phonon[cid].append(float(pn_amp[j]))
                amp_photon[cid].append(float(pt_amp[j]))

            pct = end / total_crystals * 100
            print(f"  {end}/{total_crystals} ({pct:.0f}%)", end="\r", flush=True)

    print()

    amp_phonon = {k: np.array(v) for k, v in amp_phonon.items()}
    amp_photon = {k: np.array(v) for k, v in amp_photon.items()}
    return amp_phonon, amp_photon


def make_adc_groups(all_ids, nch_per_adc):
    """crystal id → ADC 인덱스 그룹핑 (id // nch_per_adc)."""
    groups = {}
    for cid in sorted(all_ids):
        adc_idx = cid // nch_per_adc
        groups.setdefault(adc_idx, []).append(cid)
    return groups


def plot_adc(adc_idx, ids, amp_phonon, amp_photon, nbase, bins, negative, save_dir):
    NROWS, NCOLS = 4, 4
    slots_per_fig    = NROWS * NCOLS   # 16
    crystals_per_fig = slots_per_fig // 2  # 8

    amp_label = "baseline − min" if negative else "max − baseline"

    sorted_ids = sorted(ids)
    n_figs = max(1, -(-len(sorted_ids) // crystals_per_fig))

    for fig_idx in range(n_figs):
        chunk = sorted_ids[fig_idx * crystals_per_fig : (fig_idx + 1) * crystals_per_fig]

        fig, axes = plt.subplots(NROWS, NCOLS, figsize=(16, 10), squeeze=False)
        title = (f"ADC {adc_idx}  —  Triggered-Channel ADC Amplitude  "
                 f"({amp_label},  baseline={nbase} samples)")
        if n_figs > 1:
            title += f"  [{fig_idx + 1}/{n_figs}]"
        fig.suptitle(title, fontsize=12)

        for k, cid in enumerate(chunk):
            pn_vals = amp_phonon.get(cid, np.array([]))
            pt_vals = amp_photon.get(cid, np.array([]))

            for ch_idx, (vals, label, color) in enumerate([
                (pn_vals, "phonon", "steelblue"),
                (pt_vals, "photon", "tomato"),
            ]):
                slot = k * 2 + ch_idx
                r, c = divmod(slot, NCOLS)
                ax = axes[r][c]

                if len(vals) > 0:
                    ax.hist(vals, bins=bins, color=color, alpha=0.8, edgecolor="none")
                    ax.set_title(
                        f"Ch{cid} {label}\n"
                        f"μ={vals.mean():.1f}  σ={vals.std():.1f}  "
                        f"max={vals.max():.0f}  n={len(vals)}",
                        fontsize=8,
                    )
                else:
                    ax.set_title(f"Ch{cid} {label}\n(no data)", fontsize=8)

                ax.set_xlabel("amplitude [ADC counts]", fontsize=7)
                ax.set_ylabel("entries", fontsize=7)
                ax.tick_params(labelsize=7)
                ax.ticklabel_format(axis="x", style="sci", scilimits=(0, 0), useOffset=False)
                ax.xaxis.get_major_formatter().set_useMathText(True)

        used = len(chunk) * 2
        for slot in range(used, slots_per_fig):
            r, c = divmod(slot, NCOLS)
            axes[r][c].set_visible(False)

        plt.tight_layout()

        if save_dir:
            os.makedirs(save_dir, exist_ok=True)
            suffix = f"_{fig_idx + 1}" if n_figs > 1 else ""
            out = os.path.join(save_dir, f"triggered_adc_adc{adc_idx}{suffix}.png")
            plt.savefig(out, dpi=150, bbox_inches="tight")
            print(f"saved: {out}")
            plt.close(fig)
        else:
            plt.show()


def main():
    parser = argparse.ArgumentParser(
        description="Triggered-channel ADC amplitude distribution viewer",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("file",           help="HDF5 data file")
    parser.add_argument("--nbase",        type=int, default=100,
                        help="baseline 계산 샘플 수 (default: 100)")
    parser.add_argument("--nch-per-adc",  type=int, default=8, dest="nch_per_adc",
                        help="ADC 당 crystal 수 (default: 8)")
    parser.add_argument("--neg",          action="store_true",
                        help="음의 신호 방향 (baseline - min) 으로 진폭 계산")
    parser.add_argument("--bins",         type=int, default=80,
                        help="히스토그램 bin 수 (default: 80)")
    parser.add_argument("--save",         metavar="DIR", default=None,
                        help="PNG 저장 디렉토리 (생략 시 화면 표시)")
    parser.add_argument("--batch",        type=int, default=500,
                        help="한 번에 읽을 crystal 행 수 (default: 500)")
    args = parser.parse_args()

    if args.save:
        matplotlib.use("Agg")

    amp_phonon, amp_photon = collect_amplitudes(
        args.file, args.nbase, args.batch, args.neg
    )

    if not amp_phonon:
        print("ERROR: no data found in file.")
        sys.exit(1)

    all_ids = sorted(set(amp_phonon) | set(amp_photon))
    print(f"crystal IDs : {all_ids}")

    adc_groups = make_adc_groups(all_ids, args.nch_per_adc)
    print(f"ADC groups  : { {k: v for k, v in sorted(adc_groups.items())} }")

    for adc_idx, ids in sorted(adc_groups.items()):
        plot_adc(adc_idx, ids, amp_phonon, amp_photon,
                 args.nbase, args.bins, args.neg, args.save)


if __name__ == "__main__":
    main()
