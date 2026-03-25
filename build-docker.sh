#!/bin/bash
set -e

docker run --rm --name devkitpro-gamepad -v .:/mnt/ devkitpro/devkita64:20260215 sh -c "cd /mnt/ && make clean && make"
