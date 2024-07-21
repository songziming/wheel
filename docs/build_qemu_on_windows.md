在 windows 下编译 qemu

需要在 msys2 环境下编译，安装如下依赖项：

```bash
pacman -S base-devel ninja git \
    mingw-w64-x86_64-clang \
    mingw-w64-x86_64-python3 \
    glib2-devel
```

注意应该选择环境 MINGW64，只有这个环境编译出来的软件才能脱离 MSYS2 运行

```bash
pacman -S mingw-w64-x86_64-toolchain
```

```bash
pacman -S \
    mingw-w64-x86_64-meson \
    mingw-w64-x86_64-ninja \
    mingw-w64-x86_64-python \
    mingw-w64-x86_64-python-sphinx \
    mingw-w64-x86_64-python-sphinx_rtd_theme \
    mingw-w64-x86_64-autotools \
    mingw-w64-x86_64-tools-git \
    mingw-w64-x86_64-cc \
    mingw-w64-x86_64-angleproject \
    mingw-w64-x86_64-capstone \
    mingw-w64-x86_64-curl \
    mingw-w64-x86_64-cyrus-sasl \
    mingw-w64-x86_64-expat \
    mingw-w64-x86_64-fontconfig \
    mingw-w64-x86_64-freetype \
    mingw-w64-x86_64-fribidi \
    mingw-w64-x86_64-gcc-libs \
    mingw-w64-x86_64-gdk-pixbuf2 \
    mingw-w64-x86_64-gettext \
    mingw-w64-x86_64-glib2 \
    mingw-w64-x86_64-gmp \
    mingw-w64-x86_64-gnutls \
    mingw-w64-x86_64-graphite2 \
    mingw-w64-x86_64-gst-plugins-base \
    mingw-w64-x86_64-gstreamer \
    mingw-w64-x86_64-gtk3 \
    mingw-w64-x86_64-harfbuzz \
    mingw-w64-x86_64-jbigkit \
    mingw-w64-x86_64-lerc \
    mingw-w64-x86_64-libc++ \
    mingw-w64-x86_64-libdatrie \
    mingw-w64-x86_64-libdeflate \
    mingw-w64-x86_64-libepoxy \
    mingw-w64-x86_64-libffi \
    mingw-w64-x86_64-libiconv \
    mingw-w64-x86_64-libidn2 \
    mingw-w64-x86_64-libjpeg-turbo \
    mingw-w64-x86_64-libnfs \
    mingw-w64-x86_64-libpng \
    mingw-w64-x86_64-libpsl \
    mingw-w64-x86_64-libslirp \
    mingw-w64-x86_64-libssh \
    mingw-w64-x86_64-libssh2 \
    mingw-w64-x86_64-libtasn1 \
    mingw-w64-x86_64-libthai \
    mingw-w64-x86_64-libtiff \
    mingw-w64-x86_64-libunistring \
    mingw-w64-x86_64-libunwind \
    mingw-w64-x86_64-libusb \
    mingw-w64-x86_64-libwebp \
    mingw-w64-x86_64-libwinpthread-git \
    mingw-w64-x86_64-lz4 \
    mingw-w64-x86_64-lzo2 \
    mingw-w64-x86_64-nettle \
    mingw-w64-x86_64-openssl \
    mingw-w64-x86_64-opus \
    mingw-w64-x86_64-orc \
    mingw-w64-x86_64-p11-kit \
    mingw-w64-x86_64-pango \
    mingw-w64-x86_64-pixman \
    mingw-w64-x86_64-SDL2 \
    mingw-w64-x86_64-SDL2_image \
    mingw-w64-x86_64-snappy \
    mingw-w64-x86_64-spice \
    mingw-w64-x86_64-usbredir \
    mingw-w64-x86_64-xz \
    mingw-w64-x86_64-zlib \
    mingw-w64-x86_64-zstd
```


然后进入 qemu 目录：

```bash
./configure --prefix=/d/qemu --enable-sdl --enable-gtk --target-list=x86_64-softmmu --disable-werror
make -j
make install
```

必须加上 disable-werror，否则找不到 QTest Protocol 会编译失败。
