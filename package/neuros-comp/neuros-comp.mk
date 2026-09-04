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
	libxkbcommon \
	fcft \
	pixman \
	fontconfig

NEUROS_COMP_CONF_OPTS = -Dman-pages=disabled

define NEUROS_COMP_INSTALL_FONT
	$(INSTALL) -D -m 0644 $(BR2_EXTERNAL_NEUROS_PATH)/package/neuros-comp/fonts/neuros-standard.flf \
		$(TARGET_DIR)/usr/share/neuros/fonts/neuros-standard.flf
	$(INSTALL) -D -m 0644 $(BR2_EXTERNAL_NEUROS_PATH)/package/neuros-comp/fonts/neuros-banner.flf \
		$(TARGET_DIR)/usr/share/neuros/fonts/neuros-banner.flf
	$(INSTALL) -D -m 0644 $(BR2_EXTERNAL_NEUROS_PATH)/package/neuros-comp/fonts/neuros-slant.flf \
		$(TARGET_DIR)/usr/share/neuros/fonts/neuros-slant.flf
	$(INSTALL) -D -m 0644 $(BR2_EXTERNAL_NEUROS_PATH)/package/neuros-comp/fonts/ttf/Doto.ttf \
		$(TARGET_DIR)/usr/share/fonts/neuros/Doto.ttf
	$(INSTALL) -D -m 0644 $(BR2_EXTERNAL_NEUROS_PATH)/package/neuros-comp/fonts/ttf/JetBrainsMono.ttf \
		$(TARGET_DIR)/usr/share/fonts/neuros/JetBrainsMono.ttf
	$(INSTALL) -D -m 0644 $(BR2_EXTERNAL_NEUROS_PATH)/package/neuros-comp/fonts/ttf/JetBrainsMono-Bold.ttf \
		$(TARGET_DIR)/usr/share/fonts/neuros/JetBrainsMono-Bold.ttf
endef
NEUROS_COMP_POST_INSTALL_TARGET_HOOKS += NEUROS_COMP_INSTALL_FONT

$(eval $(meson-package))
