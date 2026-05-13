#!/usr/bin/env bash

# Dependencies for `make test`. The parser executable itself only uses the C
# standard library.
sudo apt-get install -y check pkg-config valgrind
