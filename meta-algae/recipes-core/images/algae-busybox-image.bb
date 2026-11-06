SUMMARY = "Minimal BusyBox-centered boot image for Algae BeaglePlay"
DESCRIPTION = "First-stage image containing only the essential userspace needed to boot and reach a BusyBox console."
LICENSE = "MIT"

inherit core-image

IMAGE_FEATURES = ""
IMAGE_LINGUAS = ""
NO_RECOMMENDATIONS = "1"

# packagegroup-core-boot supplies the minimum filesystem, init, and device setup
# required for a bootable image. Runtime providers are selected as BusyBox in
# the algae-bp configuration template.
IMAGE_INSTALL = "packagegroup-core-boot"

IMAGE_FSTYPES:remove = "tar.bz2"
