#!/system/bin/sh
# ENABLED/DISABLED reporting and recovery are owned by the common suite runtime.
exec sh "${0%/*}/suite-runtime.sh" "$@"
