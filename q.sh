#!/bin/bash
pid=$(ps aux | grep '[q]emu-system-x86_64' | awk '{print $2}')

if [ -z "$pid" ]; then
    echo "No qemu-system-x86_64 process running."
    exit 0
fi

echo "Killing qemu-system-x86_64 (pid $pid)..."
sudo kill -9 $pid
