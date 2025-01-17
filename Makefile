all: build/kitty-modeline kitty/.patched

build/build.ninja: meson.build
	meson setup build

build/kitty-modeline: build/build.ninja modeline.c
	ninja -C build

kitty/.patched: kitty.diff
	( cd kitty && patch -p1 < ../kitty.diff )
	touch kitty/.patched

clean:
	rm -rf build
