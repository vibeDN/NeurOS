################################################################################
#
# neuros-comp
#
################################################################################

NEUROS_COMP_VERSION = 0.1.0
NEUROS_COMP_SITE = $(BR2_EXTERNAL_NEUROS_PATH)/package/neuros-comp/src
NEUROS_COMP_SITE_METHOD = local
NEUROS_COMP_LICENSE = MIT
NEUROS_COMP_LICENSE_FILES = LICENSE

NEUROS_COMP_DEPENDENCIES = \
	host-pkgconf \
	host-wayland \
	wlroots \
	wayland \
	wayland-protocols \
	libxkbcommon

NEUROS_COMP_CONF_OPTS = -Dman-pages=disabled

$(eval $(meson-package))
