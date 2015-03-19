#!/bin/bash -e

if [ "$1" = "" ]; then
  echo "Usage: $0 /dev/XXX"
  exit 1
fi

DEVICE="$1"

trap "echo 'Previous step failed, aborting installation!'" EXIT

echo -n "Reading current MBR from $DEVICE ..."
dd if="$DEVICE" of=mbr count=1 bs=512
echo " done!"

echo
echo "Warning: the partition table usually contains random data in the disk identifier field."
echo "This should be set to 0 to obtain deterministic results."
echo

echo -n "Assembling boot loader ..."
head -c 440 stage1 > bootloader # TrustedGRUB stage 1 code
tail -c 72 mbr >> bootloader # disk partition table
cat stage2 >> bootloader # TrustedGRUB stage 2 code
echo " done!"

read -p "Are you sure you want to install TrustedGRUB on $DEVICE and overwrite the existing MBR? (y/n)" -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
  echo -n "Copying bootloader to $DEVICE ..."
  SIZE=`stat -c %s "bootloader"`
  dd if="bootloader" of="$DEVICE" bs="$SIZE" count=1
  echo " done!"
  trap "" EXIT
  echo "Installation successful."
else
  trap "" EXIT
  echo "Aborted by user."
  exit 1
fi
