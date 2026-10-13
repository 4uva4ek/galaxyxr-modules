#!/system/bin/sh
MODDIR=${0%/*}
sh "$MODDIR/auto.sh" off || exit 1
for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
 if [ ! -e /dev/gxr-quality-session/owned ] && [ ! -e /dev/gxr-quality-session/pid ]; then
  echo 'DISABLED - Runtime stopped; no owned override remains'; exit 0
 fi
 sleep 1
done
echo 'DISABLED - Restoration unverified. Fully remove power and restart before removing module files.'
exit 1
