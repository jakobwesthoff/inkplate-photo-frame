# Inkplate Photo Frame

A simple eInk based photo frame using the [Inkplate 6](https://inkplate.io/) from [e-radionica](https://e-radionica.com/en/).

![Photo Frame in Action](https://github.com/jakobwesthoff/inkplate-photo-frame/blob/main/docs/frame2.jpg?raw=true)
![Photo Frame in Action](https://github.com/jakobwesthoff/inkplate-photo-frame/blob/main/docs/frame3.jpg?raw=true)

## Usage

1. Clone the repository
2. Install [platform.io](https://platformio.org/)
3. Compile and upload firmware to the inkplate
4. Use my [img2inkplate](https://github.com/jakobwesthoff/img2inkplate) converter to create images in inkplate format.
5. Put those images on an FAT32 formatted SD Card into the folder `photos`.
6. Insert the SD into the inkplate and power it on.
7. Enjoy a different picture every 3 hours.

**Note:** If you want to change the interval (3h) change the value of `uS_TO_SLEEP` to a value more suitable for you.
