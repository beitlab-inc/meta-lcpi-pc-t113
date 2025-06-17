# Introduction 
This is BSP layer for [LCPI-PC-T113](https://linux-sunxi.org/LCPI-PC-T113/F113) which uses _T113-S3_ SoC.
Following preipherals are working and tested
1. `WiFi`
2. `UART0`

# Getting started
1. Clone required layers:
```bash
git clone git://git.yoctoproject.org/poky -b dunfell
cd poky/
git clone git://git.yoctoproject.org/meta-arm -b dunfell
git clone https://github.com/openembedded/meta-openembedded.git -b dunfell
git clone https://github.com/AndresJejen/meta-lcpi-pc-t113.git -b dunfell
cd ../
```
2. Export template configuraiton path and initialize build envrionment
```bash
export TEMPLATECONF=${TEMPLATECONF:-meta-lcpi-pc-t113/conf}
source poky/oe-init-build-env lcpi-pc-t113
```
3. Start build process
```bash
bitbake lcpi-pc-t113-image
```
4. Flash `wic` image into your SD card
```bash
sudo dd if=tmp/deploy/images/lcpi-pc-t113/lcpi-pc-t113-image-lcpi-pc-t113.wic of=/dev/sdX
```
5. Enjoy :-)

# More
I want to thanks all contributors of [AWboot](https://github.com/szemzoa/awboot). Their bootloader and kernel patches are directly used in this layer. Also Thanks to ArashEM https://github.com/ArashEM/meta-mangopi from which this project is inspired.
