#!/bin/bash

set -e

apt-get update
add-apt-repository -y ppa:ubuntu-toolchain-r/test
apt-get install -y g++-11
apt-get install -y python3-pip meson libtbb-dev
python3 -m pip install meson
