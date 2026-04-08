#!/usr/bin/env python3
import sys
import h5py

def print_h5(name, obj):
    if isinstance(obj, h5py.Group):
        print(f"[GROUP]   {name}")
    elif isinstance(obj, h5py.Dataset):
        print(f"[DATASET] {name}  shape={obj.shape} dtype={obj.dtype}")

def main():
    if len(sys.argv) < 2:
        print(f"usage: {sys.argv[0]} <file.h5>")
        sys.exit(1)

    fname = sys.argv[1]

    with h5py.File(fname, "r") as f:
        print(f"opened: {fname}")
        f.visititems(print_h5)

if __name__ == "__main__":
    main()
