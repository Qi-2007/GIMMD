# Better-Endfield VMD core

- Source: https://github.com/Dr-hydra/Better-Endfield
- Commit: 44612aa5e4810536b91b3bf73bec7b236883deda
- File: native/shared/motion/vmd.h
- License: AGPL-3.0-only; see LICENSE.
- Local modifications: fully validated unnamed bone records are skipped and counted as `ignored_empty_bone_keys`; malformed records remain rejected.
- The upstream header credits EIEM for record layouts and timeline semantics.

FufuLauncher source is a read-only architectural reference. Its code is not
compiled into this prototype. The Windows loader and diagnostic probe are new
implementations. This repository uses AGPL-3.0-only for the combined prototype.

