#!/usr/bin/env python3
"""
plot_rv_correlation.py — waveform + R-value(correlation) overlay for RValueTrigger

RValueTrigger.cc의 PrepareAlgo()/EvalImpl() 알고리즘을 파이썬으로 재현하여,
트리거된 waveform 위(상단)에 실제 ADC 파형을, 아래(하단)에 그 순간의
R-value(템플릿과의 슬라이딩 Pearson correlation) 궤적을 함께 그린다.

발표/설명용 그림이며, C++ 스트리밍 알고리즘의 근사 재현이다:
  - Butterworth 대역통과 필터는 scipy.signal.butter+lfilter (causal)로 대체
    (RValueTrigger.cc의 자체 IIR 설계와 동일한 표준 Butterworth 대역통과이므로
     주파수 응답은 사실상 동일하지만, 계수가 bit-exact는 아니다)
  - 나머지 (다운샘플링, 슬라이딩 윈도우 Pearson correlation, fr50 템플릿 추출)는
    RValueTrigger.cc / read_amod.C 로직을 그대로 따른다.

Usage:
    plot_rv_correlation.py <run.config> <template.root> <wvf.h5> --xid N [options]

Options:
    --evt N [N ...]   그릴 이벤트 인덱스 (default: 0)
    --light           광채널(BWFL/ltmp) 파라미터 사용 (default: heat/BWFH/htmp)
    --save PATH       저장 경로 (default: tools/figures/rv_correlation_xidNN.png)
"""

import sys
import argparse
import os

import numpy as np
import h5py
import uproot
from scipy.signal import butter, lfilter
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


def parse_config(path):
    """rv_trig CONFIG 파일의 최소 서브셋을 파싱한다.

    전역 파라미터(BWFH, BWFL, TMPRUN, NH/NL, njh/njl)와, XID -> (board, ch, THR)
    매핑을 반환한다.
    """
    glob = {}
    xid_map = {}
    cur_board = None
    cur_xids = None
    with open(path) as f:
        for line in f:
            line = line.split("#")[0].strip()
            if not line:
                continue
            toks = line.split()
            key = toks[0]
            if key == "BWFH":
                glob["bwfh"] = (int(toks[1]), float(toks[2]), float(toks[3]))
            elif key == "BWFL":
                glob["bwfl"] = (int(toks[1]), float(toks[2]), float(toks[3]))
            elif key == "TMPRUN":
                glob["tmprun"] = int(toks[1])
            elif key == "NH":
                glob["nh"], glob["njh"] = int(toks[1]), int(toks[2])
            elif key == "NL":
                glob["nl"], glob["njl"] = int(toks[1]), int(toks[2])
            elif key == "AMOREADC":
                cur_board = int(toks[1])
            elif key == "XID":
                cur_xids = [int(x) for x in toks[1:]]
                for ch, xid in enumerate(cur_xids):
                    xid_map.setdefault(xid, {})["board"] = cur_board
                    xid_map[xid]["ch"] = ch
            elif key in ("THR_RVH", "THRH_RV"):
                vals = [int(x) for x in toks[1:]]
                for xid in cur_xids:
                    xid_map[xid]["thr_h"] = vals[xid_map[xid]["ch"]]
            elif key in ("THR_RVL",):
                vals = [int(x) for x in toks[1:]]
                for xid in cur_xids:
                    xid_map[xid]["thr_l"] = vals[xid_map[xid]["ch"]]
    return glob, xid_map


def causal_bandpass(x, order, lc, uc, fs):
    b, a = butter(order, [lc, uc], btype="band", fs=fs)
    return lfilter(b, a, x)


def build_template(tmpl_file, xid, pfx, order, lc, uc, sr, ds, nwin):
    """RValueTrigger::PrepareAlgo()의 템플릿 추출을 재현.

    1) 원본 히스토그램(0.25배 스케일)에서 1000-3000 Hz BWF(order=2)로 피크를 찾고
       50% rise(fr50) 지점을 구한다.
    2) fr50-60 샘플부터 시작해 ds 간격으로 nwin개를 뽑아 raw window를 만든다.
    3) 그 window를 (order, lc, uc, sr/ds) BWF로 필터링한 것이 최종 템플릿이다.
    """
    with uproot.open(tmpl_file) as f:
        h = f[f"{pfx}{xid:02d}"]
        h0full = 0.25 * h.to_numpy()[0]

    nbins = len(h0full)
    hffull = causal_bandpass(h0full, 2, 1000.0, 3000.0, sr)

    maxx = int(np.argmax(hffull))
    fmaxv = hffull[maxx]
    fr50 = maxx
    for i in range(maxx, 0, -1):
        if hffull[i] > 0.5 * fmaxv and hffull[i - 1] <= 0.5 * fmaxv:
            fr50 = i
            break

    start = max(0, fr50 - 60)
    if start + (nwin - 1) * ds >= nbins:
        start = nbins - (nwin - 1) * ds - 1

    htmp = h0full[start: start + (nwin - 1) * ds + 1: ds]
    hftmp = causal_bandpass(htmp, order, lc, uc, sr / ds)

    mean = hftmp.mean()
    tdev = hftmp - mean
    tstd = np.sqrt(np.mean(tdev ** 2))
    return tdev, tstd


def sliding_rvalue(filt_decim, tdev, tstd):
    """decimated+filtered 스트림과 템플릿 사이의 슬라이딩 Pearson correlation."""
    n = len(tdev)
    if len(filt_decim) < n:
        return np.array([]), np.array([]), np.array([]), np.array([])
    windows = np.lib.stride_tricks.sliding_window_view(filt_decim, n)
    wmean = windows.mean(axis=1)
    wvar = (windows ** 2).mean(axis=1) - wmean ** 2
    wstd = np.sqrt(np.clip(wvar, 0, None))
    cov = (windows * tdev).mean(axis=1)  # sum(tdev)=0 -> equals covariance
    with np.errstate(divide="ignore", invalid="ignore"):
        rv = np.where((wstd > 0) & (tstd > 0), cov / (wstd * tstd), 0.0)
    idx_last = np.arange(n - 1, len(filt_decim))  # decimated index of window's newest sample
    return idx_last, rv, wmean, wstd


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("config")
    ap.add_argument("template")
    ap.add_argument("wvf_h5")
    ap.add_argument("--xid", type=int, required=True)
    ap.add_argument("--evt", type=int, nargs="+", default=[0])
    ap.add_argument("--light", action="store_true")
    ap.add_argument("--xrange", type=float, nargs=2, default=[-40, 170],
                    metavar=("LO", "HI"), help="x-axis range in ms (default: -40 170)")
    ap.add_argument("--save", default=None)
    args = ap.parse_args()

    glob, xid_map = parse_config(args.config)
    if args.xid not in xid_map:
        sys.exit(f"XID {args.xid} not found in {args.config}")

    order, lc, uc = glob["bwfl"] if args.light else glob["bwfh"]
    ds = glob["njl"] if args.light else glob["njh"]
    ntot = glob["nl"] if args.light else glob["nh"]
    nwin = ntot // ds
    pfx = "ltmp" if args.light else "htmp"
    thr = xid_map[args.xid]["thr_l" if args.light else "thr_h"] * 1e-3
    sr = 100000.0
    delay = 13000

    tdev, tstd = build_template(args.template, args.xid, pfx, order, lc, uc, sr, ds, nwin)

    key = f"xid{args.xid:02d}"
    with h5py.File(args.wvf_h5, "r") as f:
        wvf_ds = f[key]["waveform"]
        trg_ds = f[key]["trgtime"]
        RL = wvf_ds.shape[1]
        t_full_ms = np.arange(RL) / sr * 1000 - delay / sr * 1000

        fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 8.5), sharex=True,
                                        gridspec_kw={"height_ratios": [1.3, 1]})
        ax1b = ax1.twinx()
        colors = plt.cm.tab10.colors
        rv_min = 0.0
        dt_decim_ms = ds / sr * 1000
        template_z = tdev / tstd  # template, natural time order, zero-mean/unit-std

        for n, i in enumerate(args.evt):
            wvf = wvf_ds[i, :].astype(np.float64)
            wvf -= wvf[:100].mean()
            ttrg = trg_ds[i]
            color = colors[n % len(colors)]
            ax1.plot(t_full_ms, wvf, lw=1.6, alpha=0.9, color=color,
                     label="raw waveform" if n == 0 else None)

            decim = wvf[0::ds]
            t_decim_ms = t_full_ms[0::ds]
            filt = causal_bandpass(decim, order, lc, uc, sr / ds)
            idx_last, rv, wmean, wstd = sliding_rvalue(filt, tdev, tstd)
            if len(rv):
                ax2.plot(t_decim_ms[idx_last], rv, lw=1.6, alpha=0.9, color=color)
                rv_min = min(rv_min, rv.min())

                # z-score the filtered signal using the mean/std of the best-matching
                # window (peak R), so the overlay is on the same scale as the template
                # right where the two actually line up.
                best = int(np.argmax(rv))
                t_match = t_decim_ms[idx_last[best]]
                filt_z = (filt - wmean[best]) / wstd[best]
                ax1b.plot(t_decim_ms, filt_z, lw=1.2, ls="-", color=color, alpha=0.5,
                          label="Filtered signal" if n == 0 else None)

                t_template = t_match - (len(template_z) - 1 - np.arange(len(template_z))) * dt_decim_ms
                ax1b.plot(t_template, template_z, lw=2.2, ls="--", color="black",
                          label="Filtered template" if n == 0 else None)

        ax1.axvline(0, color="crimson", lw=2, ls="--", label="R-Value trigger fire time")
        ax1.set_ylabel("ADC $-$ baseline\n[ADC counts]", fontsize=13)
        ax1b.set_ylabel("filtered signal / template\n(z-score)", fontsize=11)
        ax1b.set_ylim(-4, 4)
        ax1.set_title(f"RValueTrigger — waveform & R-value ({key}, order={order}, "
                      f"BW=[{lc:.0f},{uc:.0f}] Hz, N$_{{win}}$={nwin})", fontsize=13)
        h1, l1 = ax1.get_legend_handles_labels()
        h1b, l1b = ax1b.get_legend_handles_labels()
        ax1.legend(h1 + h1b, l1 + l1b, fontsize=9, loc="upper right")
        ax1.grid(alpha=0.25)
        ax1.set_xlim(*args.xrange)

        ax2.axvline(0, color="crimson", lw=2, ls="--")
        ax2.axhline(thr, color="gray", lw=1.5, ls=":", label=f"THR = {thr:.3f}")
        ax2.fill_between(args.xrange, thr, 1.05, color="crimson", alpha=0.06)
        ax2.set_ylabel("R-value\n(template correlation)", fontsize=13)
        ax2.set_xlabel(r"t $-$ t$_{trg}$  [ms]", fontsize=13)
        ax2.legend(fontsize=10, loc="upper right")
        ax2.grid(alpha=0.25)
        ax2.set_ylim(min(rv_min, -0.05) - 0.05, 1.05)
        ax2.set_xlim(*args.xrange)

        for ax in (ax1, ax2):
            ax.tick_params(labelsize=11)

        fig.tight_layout()
        out = args.save or f"tools/figures/rv_correlation_{key}.png"
        os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
        fig.savefig(out, dpi=200)
        print(f"saved: {out}")


if __name__ == "__main__":
    main()
