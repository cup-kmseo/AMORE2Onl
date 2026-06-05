#!/usr/bin/env python3
"""
read_rv_waveform.py — waveform viewer for rv_trigger_test output

HDF5 structure:
  /xid{NN}/trgtime  [N]       float64  trigger time (ms from file start)
  /xid{NN}/waveform [N, RL]   uint16   ADC/4, trigger at sample RL-DT

Usage:
    read_rv_waveform.py <file.wvf.h5> [options]

Options:
    --list             print trigger count per XID and exit
    --xid N [N ...]    XIDs to display (default: all)
    --evt N [N ...]    event indices to display (default: overlay 0~nmax)
    --nmax N           max waveforms to overlay (default: 5)
    --delay N          pre-trigger samples = RL-DT (default: 29000)
    --sr N             sampling rate in Hz (default: 100000)
    --sub-baseline     subtract mean of first 100 samples
    --save DIR         save PNG to DIR instead of current directory
"""

import sys
import argparse
import os

import h5py
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


def list_xids(f):
    print(f"{'XID':<6} {'N_trg':>6}")
    print("-" * 14)
    for key in sorted(f.keys(), key=lambda k: int(k[3:])):
        n = f[key]['trgtime'].shape[0]
        print(f"{key:<6} {n:>6}")


def plot_xid(ax, f, xid_key, evt_indices, delay_ms, sr, sub_baseline, nmax):
    wvf_ds = f[xid_key]['waveform']
    trg_ds = f[xid_key]['trgtime']
    N      = wvf_ds.shape[0]
    RL     = wvf_ds.shape[1]

    if evt_indices is None:
        indices = list(range(min(nmax, N)))
    else:
        indices = [i for i in evt_indices if 0 <= i < N]
        if not indices:
            ax.set_title(f"{xid_key} — no valid indices")
            return

    t_ms = np.arange(RL) / sr * 1000 - delay_ms  # ms relative to trigger

    for i in indices[:nmax]:
        wvf = wvf_ds[i, :].astype(np.float32)
        if sub_baseline:
            baseline = wvf[:100].mean()
            wvf -= baseline
        ttrg = trg_ds[i]
        ax.plot(t_ms, wvf, lw=0.7, alpha=0.8, label=f"evt{i} t={ttrg:.1f}ms")

    ax.axvline(0, color='r', lw=0.8, ls='--')
    n_shown = len(indices[:nmax])
    ax.set_title(f"{xid_key}  (N={N}, showing {n_shown})", fontsize=10)
    ax.set_xlabel("t − t_trg (ms)", fontsize=8)
    ylabel = "ADC − baseline" if sub_baseline else "ADC (14-bit)"
    ax.set_ylabel(ylabel, fontsize=8)
    ax.tick_params(labelsize=7)
    if n_shown <= 6:
        ax.legend(fontsize=6)


def main():
    parser = argparse.ArgumentParser(
        description="waveform viewer for rv_trigger_test output",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("file",           help="*.wvf.h5 file")
    parser.add_argument("--list",         action="store_true",
                        help="print trigger count per XID and exit")
    parser.add_argument("--xid",          type=int, nargs="+", metavar="N",
                        help="XIDs to display (default: all)")
    parser.add_argument("--evt",          type=int, nargs="+", metavar="N",
                        help="event indices to display (default: overlay 0~nmax)")
    parser.add_argument("--nmax",         type=int, default=5,
                        help="max waveforms to overlay (default: 5)")
    parser.add_argument("--delay",        type=int, default=13000,
                        help="pre-trigger samples = DELAY (default: 13000)")
    parser.add_argument("--sr",           type=int, default=100000,
                        help="sampling rate in Hz (default: 100000)")
    parser.add_argument("--sub-baseline", action="store_true",
                        help="subtract mean of first 100 samples")
    parser.add_argument("--save",         metavar="DIR", default=None,
                        help="save PNG to DIR (default: current directory)")
    args = parser.parse_args()

    delay_ms = args.delay / args.sr * 1000

    with h5py.File(args.file, "r") as f:
        if args.list:
            list_xids(f)
            return

        all_keys = sorted(f.keys(), key=lambda k: int(k[3:]))

        if args.xid is not None:
            keys = [f"xid{x:02d}" for x in args.xid if f"xid{x:02d}" in f]
            missing = [x for x in args.xid if f"xid{x:02d}" not in f]
            if missing:
                print(f"warning: XID {missing} not found in file", file=sys.stderr)
        else:
            keys = all_keys

        if not keys:
            print("ERROR: no matching XIDs found.", file=sys.stderr)
            sys.exit(1)

        ncols = 6
        nrows = -(-len(keys) // ncols)  # ceiling div
        fig, axes = plt.subplots(nrows, ncols,
                                 figsize=(5 * ncols, 4 * nrows),
                                 squeeze=False)
        fig.suptitle(
            f"{os.path.basename(args.file)}\n"
            f"delay={args.delay} samples ({delay_ms:.0f} ms)  "
            f"SR={args.sr/1000:.0f} kHz",
            fontsize=11,
        )

        for idx, key in enumerate(keys):
            r, c = divmod(idx, ncols)
            plot_xid(axes[r][c], f, key,
                     args.evt, delay_ms, args.sr,
                     args.sub_baseline, args.nmax)

        for idx in range(len(keys), nrows * ncols):
            r, c = divmod(idx, ncols)
            axes[r][c].set_visible(False)

        plt.tight_layout()

        save_dir = args.save if args.save else "."
        os.makedirs(save_dir, exist_ok=True)
        xid_tag = ("_".join(f"xid{x:02d}" for x in args.xid)
                   if args.xid else "all")
        base = os.path.splitext(os.path.basename(args.file))[0]
        out  = os.path.join(save_dir, f"{base}_{xid_tag}.png")
        plt.savefig(out, dpi=150, bbox_inches="tight")
        print(f"saved: {out}")
        plt.close(fig)


if __name__ == "__main__":
    main()
