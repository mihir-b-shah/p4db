#!/bin/bash

set -e

sudo apt-get update
sudo add-apt-repository -y ppa:ubuntu-toolchain-r/test
sudo apt-get install -y g++-11
sudo apt-get install -y python3-pip meson libtbb-dev
python3 -m pip install meson
