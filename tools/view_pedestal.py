#!/usr/bin/env python3
"""
view_pedestal.py -- pedestal distribution viewer, one figure per ADC

Crystal ID 기준으로 ADC를 그룹핑하여 각 ADC 당 한 장씩 출력.
각 subplot 에 phonon / photon pedestal 히스토그램과 μ, σ 표시.

Usage:
    view_pedestal.py <file.h5> [options]

Options:
    --nbase N         pedestal 계산에 쓸 앞쪽 샘플 수 (default: 100)
    --nch-per-adc N   ADC 당 crystal 수 (channel pairs, default: 8 = 16ch/2)
    --save DIR        대화형 표시 대신 DIR 에 PNG 저장
    --batch N         phonon/photon 한 번에 읽을 crystal 행 수 (default: 1000)
"""

import sys
import argparse
import os
import numpy as np
import matplotlib
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import h5py


def collect_pedestals(fname, nbase, batch_size):
    """
    HDF5 파일에서 crystal 별 phonon/photon pedestal 값 목록을 수집.

    Returns
    -------
    ped_phonon : dict  {crystal_id: np.ndarray of pedestal values}
    ped_photon : dict
    """
    ped_phonon = {}
    ped_photon = {}

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
        print(f"nbase          : {nb}")
        print(f"reading in batches of {batch_size} crystals ...", flush=True)

        # chs 전체 읽기 (small: id + ttime per crystal)
        all_chs = chs_ds[:]          # shape (total_crystals,)
        all_ids = all_chs["id"].astype(np.int32)

        for start in range(0, total_crystals, batch_size):
            end = min(start + batch_size, total_crystals)
            # phonon/photon 첫 nb 샘플만 슬라이스
            pn_batch = phonon_ds[start:end, :nb].astype(np.float32)  # (batch, nb)
            pt_batch = photon_ds[start:end, :nb].astype(np.float32)

            pn_mean = pn_batch.mean(axis=1)   # (batch,)
            pt_mean = pt_batch.mean(axis=1)

            ids_batch = all_ids[start:end]

            for j, cid in enumerate(ids_batch):
                cid = int(cid)
                if cid not in ped_phonon:
                    ped_phonon[cid] = []
                    ped_photon[cid] = []
                ped_phonon[cid].append(float(pn_mean[j]))
                ped_photon[cid].append(float(pt_mean[j]))

            pct = end / total_crystals * 100
            print(f"  {end}/{total_crystals} ({pct:.0f}%)", end="\r", flush=True)

    print()

    # list → ndarray
    ped_phonon = {k: np.array(v) for k, v in ped_phonon.items()}
    ped_photon = {k: np.array(v) for k, v in ped_photon.items()}
    return ped_phonon, ped_photon


def make_adc_groups(all_ids, nch_per_adc):
    """crystal id → ADC 인덱스 그룹핑 (id // nch_per_adc)."""
    groups = {}
    for cid in sorted(all_ids):
        adc_idx = cid // nch_per_adc
        groups.setdefault(adc_idx, []).append(cid)
    return groups  # {adc_idx: [cid, ...]}


def plot_adc(adc_idx, ids, ped_phonon, ped_photon, nbase, save_dir):
    # 4×4 고정 격자.  각 crystal → 2 칸 (phonon, photon) 순서로 채움.
    # 최대 8 crystal (16 채널) 까지 한 장에 표시; 넘으면 다음 장으로 분할.
    NROWS, NCOLS = 4, 4
    slots_per_fig = NROWS * NCOLS          # 16 슬롯 = 8 crystal
    crystals_per_fig = slots_per_fig // 2  # 8

    sorted_ids = sorted(ids)
    n_figs = max(1, -(-len(sorted_ids) // crystals_per_fig))  # ceiling div

    for fig_idx in range(n_figs):
        chunk = sorted_ids[fig_idx * crystals_per_fig : (fig_idx + 1) * crystals_per_fig]

        fig, axes = plt.subplots(NROWS, NCOLS, figsize=(16, 10), squeeze=False)
        title = f"ADC {adc_idx}  —  Pedestal Distribution  (first {nbase} samples)"
        if n_figs > 1:
            title += f"  [{fig_idx + 1}/{n_figs}]"
        fig.suptitle(title, fontsize=13)

        # 슬롯 순서: (row, col) → (0,0),(0,1),(0,2),(0,3),(1,0),...
        # crystal k → slot 2k (phonon), 2k+1 (photon)
        for k, cid in enumerate(chunk):
            pn_vals = ped_phonon.get(cid, np.array([]))
            pt_vals = ped_photon.get(cid, np.array([]))

            for ch_idx, (vals, label, color) in enumerate([
                (pn_vals, "phonon", "steelblue"),
                (pt_vals, "photon", "tomato"),
            ]):
                slot = k * 2 + ch_idx
                r, c = divmod(slot, NCOLS)
                ax = axes[r][c]

                if len(vals) > 0:
                    ax.hist(vals, bins=60, color=color, alpha=0.8, edgecolor="none")
                    ax.set_title(
                        f"Ch{cid} {label}\n"
                        f"μ={vals.mean():.1f}  σ={vals.std():.1f}  n={len(vals)}",
                        fontsize=8,
                    )
                else:
                    ax.set_title(f"Ch{cid} {label}\n(no data)", fontsize=8)
                ax.set_xlabel("ADC count", fontsize=7)
                ax.set_ylabel("entries",   fontsize=7)
                ax.tick_params(labelsize=7)
                ax.ticklabel_format(axis="x", style="sci", scilimits=(0, 0), useOffset=False)
                ax.xaxis.get_major_formatter().set_useMathText(True)

        # 남은 빈 슬롯 숨기기
        used = len(chunk) * 2
        for slot in range(used, slots_per_fig):
            r, c = divmod(slot, NCOLS)
            axes[r][c].set_visible(False)

        plt.tight_layout()

        if save_dir:
            os.makedirs(save_dir, exist_ok=True)
            suffix = f"_{fig_idx + 1}" if n_figs > 1 else ""
            out = os.path.join(save_dir, f"pedestal_adc{adc_idx}{suffix}.png")
            plt.savefig(out, dpi=150, bbox_inches="tight")
            print(f"saved: {out}")
            plt.close(fig)
        else:
            plt.show()


def main():
    parser = argparse.ArgumentParser(
        description="Pedestal viewer: one figure per ADC",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("file",          help="HDF5 data file")
    parser.add_argument("--nbase",       type=int, default=100,
                        help="pedestal 계산 샘플 수 (default: 100)")
    parser.add_argument("--nch-per-adc", type=int, default=8, dest="nch_per_adc",
                        help="ADC 당 crystal 수 (default: 8)")
    parser.add_argument("--save",        metavar="DIR", default=None,
                        help="PNG 저장 디렉토리 (생략 시 화면 표시)")
    parser.add_argument("--batch",       type=int, default=1000,
                        help="한 번에 읽을 crystal 행 수 (default: 1000)")
    args = parser.parse_args()

    if args.save:
        matplotlib.use("Agg")

    ped_phonon, ped_photon = collect_pedestals(args.file, args.nbase, args.batch)

    if not ped_phonon:
        print("ERROR: no data found in file.")
        sys.exit(1)

    all_ids = sorted(set(ped_phonon) | set(ped_photon))
    print(f"crystal IDs : {all_ids}")

    adc_groups = make_adc_groups(all_ids, args.nch_per_adc)
    print(f"ADC groups  : { {k: v for k, v in sorted(adc_groups.items())} }")

    for adc_idx, ids in sorted(adc_groups.items()):
        plot_adc(adc_idx, ids, ped_phonon, ped_photon, args.nbase, args.save)


if __name__ == "__main__":
    main()
