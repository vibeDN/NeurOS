################################################################################
#
# fastfetch
#
################################################################################

FASTFETCH_VERSION = 2.68.1
FASTFETCH_SITE = $(call github,fastfetch-cli,fastfetch,$(FASTFETCH_VERSION))
FASTFETCH_LICENSE = MIT
FASTFETCH_LICENSE_FILES = LICENSE
FASTFETCH_DEPENDENCIES = host-pkgconf

# minimal build - drop every optional detector/backend
FASTFETCH_CONF_OPTS = \
	-DBUILD_TESTS=OFF \
	-DENABLE_LTO=OFF \
	-DENABLE_SYSTEM_YYJSON=OFF \
	-DENABLE_ZLIB=OFF \
	-DENABLE_VULKAN=OFF \
	-DENABLE_WAYLAND=OFF \
	-DENABLE_XCB_RANDR=OFF \
	-DENABLE_XRANDR=OFF \
	-DENABLE_DRM=OFF \
	-DENABLE_VADRM=OFF \
	-DENABLE_VAX11=OFF \
	-DENABLE_VDPAU=OFF \
	-DENABLE_GIO=OFF \
	-DENABLE_DCONF=OFF \
	-DENABLE_EET=OFF \
	-DENABLE_DBUS=OFF \
	-DENABLE_SQLITE3=OFF \
	-DENABLE_RPM=OFF \
	-DENABLE_IMAGEMAGICK7=OFF \
	-DENABLE_IMAGEMAGICK6=OFF \
	-DENABLE_CHAFA=OFF \
	-DENABLE_EGL=OFF \
	-DENABLE_GLX=OFF \
	-DENABLE_OPENCL=OFF \
	-DENABLE_FREETYPE=OFF \
	-DENABLE_PULSE=OFF \
	-DENABLE_DDCUTIL=OFF \
	-DENABLE_ELF=OFF \
	-DENABLE_LUA=OFF \
	-DENABLE_QUICKJS=OFF \
	-DENABLE_LIBZFS=OFF

# The Bootlin external toolchain ships 5.4 kernel headers, whose
# <drm/nouveau_drm.h> predates struct drm_nouveau_getparam; fastfetch's
# gpu_drm.c compiles unconditionally when <drm/drm.h> exists. We don't surface
# GPU info anyway - stub the six exported detectors.
define FASTFETCH_STUB_GPU_DRM
	{ \
	  echo '#include "gpu.h"'; \
	  echo 'const char* ffDrmDetectRadeon(const FFGPUOptions* o, FFGPUResult* g, const char* p){(void)o;(void)g;(void)p;return "disabled";}'; \
	  echo 'const char* ffDrmDetectAmdgpu(const FFGPUOptions* o, FFGPUResult* g, const char* p){(void)o;(void)g;(void)p;return "disabled";}'; \
	  echo 'const char* ffDrmDetectI915(FFGPUResult* g, int fd){(void)g;(void)fd;return "disabled";}'; \
	  echo 'const char* ffDrmDetectXe(FFGPUResult* g, int fd){(void)g;(void)fd;return "disabled";}'; \
	  echo 'const char* ffDrmDetectAsahi(FFGPUResult* g, int fd){(void)g;(void)fd;return "disabled";}'; \
	  echo 'const char* ffDrmDetectNouveau(FFGPUResult* g, int fd){(void)g;(void)fd;return "disabled";}'; \
	  echo 'const char* ffGPUDetectDriverSpecific(const FFGPUOptions* o, FFGPUResult* g, FFGpuDriverPciBusId b){(void)o;(void)g;(void)b;return "disabled";}'; \
	} > $(@D)/src/detection/gpu/gpu_drm.c
endef
FASTFETCH_POST_EXTRACT_HOOKS += FASTFETCH_STUB_GPU_DRM

$(eval $(cmake-package))
