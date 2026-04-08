#!/usr/bin/env python3
import sys
import h5py
import numpy as np
import matplotlib.pyplot as plt


def main():
    if len(sys.argv) < 2:
        print(f"usage: {sys.argv[0]} <file.h5> [event_index]")
        sys.exit(1)

    fname = sys.argv[1]
    evt = int(sys.argv[2]) if len(sys.argv) > 2 else 0

    with h5py.File(fname, "r") as f:
        info_ds   = f["events/info"]
        chs_ds    = f["events/chs"]
        phonon_ds = f["events/phonon"]
        photon_ds = f["events/photon"]
        index_ds  = f["events/index"]
        subrun_ds = f["subrun"]

        nevt = info_ds.shape[0]
        nsamp = phonon_ds.shape[1]

        if evt < 0 or evt >= nevt:
            print(f"invalid event index: {evt} (allowed: 0 ~ {nevt-1})")
            sys.exit(1)

        info = info_ds[evt]
        chs = chs_ds[evt]
        phonon = phonon_ds[evt]
        photon = photon_ds[evt]
        evindex = index_ds[evt]

        print("=" * 80)
        print(f"file        : {fname}")
        print(f"event index : {evt}")
        print(f"n_events    : {nevt}")
        print(f"n_samples   : {nsamp}")
        print()

        print("[subrun]")
        for i, row in enumerate(subrun_ds):
            print(
                f"  subrun[{i}] : "
                f"subrun={row['subrun']} "
                f"nevent={row['nevent']} "
                f"first={row['first']} "
                f"last={row['last']}"
            )
        print()

        print("[event/info]")
        print(f"  index : {evindex}")
        print(f"  ttype : {info['ttype']}")
        print(f"  nhit  : {info['nhit']}")
        print(f"  tnum  : {info['tnum']}")
        print(f"  ttime : {info['ttime']}")
        print()

        print("[event/chs]")
        print(f"  id    : {chs['id']}")
        print(f"  ttime : {chs['ttime']}")
        print()

        print("[phonon]")
        print(f"  shape      : {phonon.shape}")
        print(f"  dtype      : {phonon.dtype}")
        print(f"  min / max  : {phonon.min()} / {phonon.max()}")
        print(f"  mean       : {phonon.mean():.3f}")
        print(f"  first 50   : {phonon[:50]}")
        print()

        print("[photon]")
        print(f"  shape      : {photon.shape}")
        print(f"  dtype      : {photon.dtype}")
        print(f"  min / max  : {photon.min()} / {photon.max()}")
        print(f"  mean       : {photon.mean():.3f}")
        print(f"  first 50   : {photon[:50]}")
        print()

        # 원하면 baseline 비슷한 것도 간단히 확인
        nbase = min(100, len(phonon))
        p_base = np.mean(phonon[:nbase])
        g_base = np.mean(photon[:nbase])

        print("[quick baseline estimate]")
        print(f"  phonon baseline (~first {nbase}) : {p_base:.3f}")
        print(f"  photon baseline (~first {nbase}) : {g_base:.3f}")
        print("=" * 80)

    # 플롯은 파일 닫은 뒤 진행
    plt.figure(figsize=(12, 4))
    plt.plot(phonon, label="phonon")
    plt.plot(photon, label="photon")
    plt.xlabel("sample")
    plt.ylabel("ADC")
    plt.title(
        f"Waveforms: evt={evt}, tnum={info['tnum']}, "
        f"ttype={info['ttype']}, ttime={info['ttime']}"
    )
    plt.legend()
    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    main()
