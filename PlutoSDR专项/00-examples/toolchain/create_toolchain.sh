#!/bin/bash

wget https://developer.arm.com/-/cdn-downloads/permalink/legacy-linaro-gnu-toolchains/7.3-2018.05/gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf.tar.xz
tar -xJf gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf.tar.xz
rm gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf.tar.xz

mkdir libiio
wget https://github.com/analogdevicesinc/libiio/releases/download/v0.25/libiio-0.25.gb6028fd-Ubuntu-arm32v7.tar.gz
tar -xzf libiio-0.25.gb6028fd-Ubuntu-arm32v7.tar.gz -C libiio
rm -rf libiio-0.25.gb6028fd-Ubuntu-arm32v7.tar.gz

