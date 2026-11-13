# Historical standalone compile recipe

`rebuild_obj.sh` preserves the earlier nested-XNU compile recipe unchanged.
Its old path and flags are evidence, not the current build entry point.
Use `../rebuild-i9100.py` for the active repository, or the loader's
`scripts/i9100/prepare-automation.py` for the complete dependent object list.
