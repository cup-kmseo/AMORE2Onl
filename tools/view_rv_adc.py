#!/usr/bin/env python3
"""
view_rv_adc.py — ADC amplitude distribution viewer for rv_trigger_test waveforms

트리거된 이벤트 waveform에서 진폭(peak - baseline)을 계산하여
XID별 히스토그램으로 출력한다.

  baseline  = waveform 앞 nbase 샘플의 평균
  amplitude = max(waveform) - baseline       (양의 신호, 기본값)
              또는 baseline - min(waveform)  (--neg 플래그)

여러 파일을 지정하면 XID별로 누적하여 하나의 분포를 그린다.

Usage:
    view_rv_adc.py <file.wvf.h5> [file2.h5 ...] [options]

Options:
    --nbase N       baseline 계산에 쓸 앞쪽 샘플 수 (default: 100)
    --neg           음의 신호 방향 (baseline - min)
    --bins N        히스토그램 bin 수 (default: 60)
    --xid N [N ...] 표시할 XID (default: all)
    --range LO HI   x축 범위 [ADC counts] (default: auto)
    --save DIR      PNG 저장 디렉토리 (생략 시 현재 디렉토리)
"""

import sys
import argparse
import os
import glob
from collections import defaultdict

import numpy as np
import h5py
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


def collect_amplitudes(fnames, xid_filter, nbase, negative):
    """
    파일 목록에서 XID별 진폭 배열을 수집.

    Returns
    -------
    amps   : dict  {xid_int: np.ndarray of amplitude values}
    n_evts : dict  {xid_int: total event count}
    """
    amps   = defaultdict(list)
    n_evts = defaultdict(int)

    for fname in fnames:
        with h5py.File(fname, "r") as f:
            all_keys = sorted(f.keys(), key=lambda k: int(k[3:]))
            if xid_filter is not None:
                keys = [f"xid{x:02d}" for x in xid_filter if f"xid{x:02d}" in f]
            else:
                keys = all_keys

            for key in keys:
                xid = int(key[3:])
                wvf_ds = f[key]["waveform"]
                N, RL  = wvf_ds.shape
                nb     = min(nbase, RL)

                wvf = wvf_ds[:].astype(np.float32)      # (N, RL)
                baseline = wvf[:, :nb].mean(axis=1)     # (N,)

                if negative:
                    amp = baseline - wvf.min(axis=1)
                else:
                    amp = wvf.max(axis=1) - baseline

                amps[xid].extend(amp.tolist())
                n_evts[xid] += N

        print(f"  loaded: {os.path.basename(fname)}", flush=True)

    return (
        {k: np.array(v) for k, v in amps.items()},
        dict(n_evts),
    )


def plot_distributions(amps, n_evts, nbase, bins, negative, xrange, save_dir, tag):
    all_xids = sorted(amps.keys())
    if not all_xids:
        print("ERROR: no data to plot.")
        sys.exit(1)

    ncols = 6
    nrows = -(-len(all_xids) // ncols)

    fig, axes = plt.subplots(nrows, ncols,
                             figsize=(4.5 * ncols, 3.5 * nrows),
                             squeeze=False)

    amp_label = "baseline − min" if negative else "max − baseline"
    fig.suptitle("Triggered-Event ADC Amplitude Distribution", fontsize=11)

    for idx, xid in enumerate(all_xids):
        r, c  = divmod(idx, ncols)
        ax    = axes[r][c]
        vals  = amps[xid]
        n_tot = n_evts.get(xid, len(vals))

        if len(vals) == 0:
            ax.set_title(f"XID {xid:02d}\n(no data)", fontsize=9)
            ax.set_visible(True)
            continue

        hist_range = xrange if xrange else None
        ax.hist(vals, bins=bins, range=hist_range,
                color="steelblue", alpha=0.85, edgecolor="none")

        ax.set_title(f"XID {xid:02d}  n={n_tot}", fontsize=8)
        ax.set_xlabel("amplitude [ADC counts]", fontsize=7)
        ax.set_ylabel("entries", fontsize=7)
        ax.set_yscale("log")
        ax.tick_params(labelsize=7)
        if xrange:
            ax.set_xlim(xrange)

    for idx in range(len(all_xids), nrows * ncols):
        r, c = divmod(idx, ncols)
        axes[r][c].set_visible(False)

    plt.tight_layout()

    os.makedirs(save_dir, exist_ok=True)
    out = os.path.join(save_dir, f"{tag}_adc_dist.png")
    plt.savefig(out, dpi=150, bbox_inches="tight")
    print(f"saved: {out}")
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(
        description="ADC amplitude distribution viewer for rv_trigger_test waveforms",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("files",          nargs="+",
                        help="*.wvf.h5 파일 (glob 가능)")
    parser.add_argument("--nbase",        type=int, default=100,
                        help="baseline 계산 샘플 수 (default: 100)")
    parser.add_argument("--neg",          action="store_true",
                        help="음의 신호 방향 (baseline - min)")
    parser.add_argument("--bins",         type=int, default=60,
                        help="히스토그램 bin 수 (default: 60)")
    parser.add_argument("--xid",          type=int, nargs="+", metavar="N",
                        help="표시할 XID (default: all)")
    parser.add_argument("--range",        type=float, nargs=2, metavar=("LO", "HI"),
                        dest="xrange",
                        help="x축 범위 [ADC counts]")
    parser.add_argument("--save",         metavar="DIR", default=".",
                        help="PNG 저장 디렉토리 (default: 현재 디렉토리)")
    args = parser.parse_args()

    # glob 확장
    fnames = []
    for pattern in args.files:
        expanded = sorted(glob.glob(pattern))
        if expanded:
            fnames.extend(expanded)
        else:
            fnames.append(pattern)

    fnames = sorted(set(fnames))
    missing = [f for f in fnames if not os.path.exists(f)]
    if missing:
        print(f"ERROR: file(s) not found: {missing}", file=sys.stderr)
        sys.exit(1)

    print(f"files   : {len(fnames)}")
    print(f"nbase   : {args.nbase}")
    direction = "baseline - min (neg)" if args.neg else "max - baseline (pos)"
    print(f"amplitude: {direction}")
    print(f"bins    : {args.bins}")
    if args.xid:
        print(f"XIDs    : {args.xid}")
    print("collecting amplitudes ...")

    amps, n_evts = collect_amplitudes(fnames, args.xid, args.nbase, args.neg)

    total = sum(len(v) for v in amps.values())
    print(f"\nXIDs found : {sorted(amps.keys())}")
    print(f"total evts : {total}")

    # 출력 파일 이름 태그: 첫 파일 기반 run 번호 + 파일 수
    base0  = os.path.splitext(os.path.splitext(os.path.basename(fnames[0]))[0])[0]
    # e.g. "rv_test_000657_00000"  → strip trailing _NNNNN part
    run_tag = "_".join(base0.split("_")[:4]) if len(base0.split("_")) >= 4 else base0
    if len(fnames) > 1:
        run_tag += f"_x{len(fnames)}files"

    plot_distributions(amps, n_evts, args.nbase, args.bins, args.neg,
                       args.xrange, args.save, run_tag)


if __name__ == "__main__":
    main()
