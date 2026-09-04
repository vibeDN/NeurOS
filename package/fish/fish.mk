################################################################################
#
# fish
#
################################################################################

FISH_VERSION = 3.7.1
FISH_SITE = https://github.com/fish-shell/fish-shell/releases/download/$(FISH_VERSION)
FISH_SOURCE = fish-$(FISH_VERSION).tar.xz
FISH_LICENSE = GPL-2.0
FISH_LICENSE_FILES = COPYING
FISH_DEPENDENCIES = ncurses pcre2 host-pkgconf

# 4.x switched to Rust; stay on the last C++ line. Docs need Sphinx (host py).
FISH_CONF_OPTS = \
	-DBUILD_TESTING=OFF \
	-DBUILD_DOCS=OFF \
	-DINSTALL_DOCS=OFF \
	-DFISH_USE_SYSTEM_PCRE2=ON \
	-DWITH_GETTEXT=OFF \
	-DCMAKE_BUILD_TYPE=Release

# curses detection: point it at Buildroot's ncurses (wide)
FISH_CONF_OPTS += -DCURSES_NEED_NCURSES=TRUE

# fish 3.7.1 predates CMake 3.30: cmake/Tests.cmake pushes CMP0037=OLD to define
# an alias `test` target, both of which modern CMake rejects. Excise the whole
# PUSH..POP block (lines 46-51); the rest of Tests.cmake is still needed.
define FISH_DROP_TEST_ALIAS
	$(SED) '46,51d' $(@D)/cmake/Tests.cmake
endef
FISH_PRE_CONFIGURE_HOOKS += FISH_DROP_TEST_ALIAS

$(eval $(cmake-package))
