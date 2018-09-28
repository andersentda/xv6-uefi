To build:

Run `make efi` to compile.
Run `make qemu-efi` to run.

There are also efimemfs and efi-gdb targets.

A 32-bit version of OVMF is included, but you can build your own from source using TianoCore's implementation.

You need to have mkgpt installed. Follow these instructions:

~~~
sudo apt-get install qemu binutils-mingw-w64 gcc-mingw-w64 xorriso mtools gnu-efi
wget http://www.tysos.org/files/efi/mkgpt-latest.tar.bz2
tar jxf mkgpt-latest.tar.bz2
cd mkgpt && ./configure && make && sudo make install && cd ..
~~~
