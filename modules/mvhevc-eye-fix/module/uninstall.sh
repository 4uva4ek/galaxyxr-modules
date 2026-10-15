#!/system/bin/sh
# KernelSU runs this in post-fs-data of the boot that removes the module, before
# pm, settings, cmd or am exist. The bind mounts this module creates do not
# survive a reboot, so the stock libraries are already in use by then and there is
# nothing to unmount here. A removal while the headset keeps running is handled by
# the watcher service.sh leaves behind, which unbinds within seconds of the remove
# marker or the directory disappearing; nothing here needs Android services.
exit 0
