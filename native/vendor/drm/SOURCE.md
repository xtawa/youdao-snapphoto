Linux DRM UAPI headers reused from the existing device screenshot tooling.
Upstream: https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/include/uapi/drm?h=v5.10
Used only to allocate application-owned dumb buffers and export DMA-BUF fds; no modesetting or display property writes.
