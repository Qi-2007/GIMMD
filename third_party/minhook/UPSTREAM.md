# MinHook

- Source: https://github.com/TsudaKageyu/minhook
- Tag: v1.3.4
- License: BSD-2-Clause; see LICENSE.txt (includes HDE copyright notices).
- Local modifications: none.
- Used only by the opt-in bounded runtime camera observer.

The observer disables its one hook after sampling, verifies original entry
bytes, and retains the inactive trampoline until process exit to avoid freeing
code under a delayed callback. No global MH_ALL_HOOKS operation is used.
