#!/bin/sh

qemu-system-x86_64 -enable-kvm -smp 4 -m 512 -boot c -nographic -net nic -net user,hostfwd=tcp::10022-:22 -kernel /home/sebastiano/litmus-rt/arch/x86/boot/bzImage -append "console=ttyS0,115200 root=/dev/hda1" -hda /home/sebastiano/WorkspaceLitmus/ubuntu.backing.qcow2.img
