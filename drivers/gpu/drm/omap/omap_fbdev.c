/*
 * linux/drivers/gpu/drm/omap/omap_fbdev.c
 *
 * Copyright (C) 2011 Texas Instruments
 * Author: Rob Clark <rob@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef CONFIG_DRM_OMAP_FB_OMAP2_COMPAT
#  include <linux/omapfb.h>
#endif

#include <linux/omap_gpu.h>
#include "omap_gpu_priv.h"

#include "drm_crtc.h"
#include "drm_fb_helper.h"


/*
 * fbdev funcs, to implement legacy fbdev interface on top of drm driver
 */

#define to_omap_fbdev(x) container_of(x, struct omap_fbdev, base)

struct omap_fbdev {
	struct drm_fb_helper base;
	struct drm_framebuffer *fb;
};

static struct drm_fb_helper *get_fb(struct fb_info *fbi)
{
	if (!fbi || strcmp(fbi->fix.id, MODULE_NAME)) {
		/* these are not the fb's you're looking for */
		return NULL;
	}
	return fbi->par;
}

static ssize_t omap_fbdev_write(struct fb_info *fbi, const char __user *buf,
		size_t count, loff_t *ppos)
{
	ssize_t res;

	res = fb_sys_write(fbi, buf, count, ppos);
	omap_fbdev_flush(fbi, 0, 0, fbi->var.xres, fbi->var.yres);

	return res;
}

static void omap_fbdev_fillrect(struct fb_info *fbi,
		const struct fb_fillrect *rect)
{
	sys_fillrect(fbi, rect);
	omap_fbdev_flush(fbi, rect->dx, rect->dy, rect->width, rect->height);
}

static void omap_fbdev_copyarea(struct fb_info *fbi,
		const struct fb_copyarea *area)
{
	sys_copyarea(fbi, area);
	omap_fbdev_flush(fbi, area->dx, area->dy, area->width, area->height);
}

static void omap_fbdev_imageblit(struct fb_info *fbi,
		const struct fb_image *image)
{
	sys_imageblit(fbi, image);
	omap_fbdev_flush(fbi, image->dx, image->dy, image->width, image->height);
}

#ifdef CONFIG_DRM_OMAP_FB_OMAP2_COMPAT
#  ifdef CONFIG_FB_OMAP2_REG_IOCTL
static int omap_fbdev_reg_read(struct omap_dss_device *display,
				   struct omapfb_reg_access *reg_read)
{
	int r = 0;
	u8 *buf;

	if (display == NULL)
		return -ENODEV;

	if (!display->driver->reg_read)
		return -EINVAL;

	if (!access_ok(VERIFY_WRITE, reg_read->buffer, reg_read->buffer_size))
		return -EFAULT;

	buf = kmalloc(reg_read->buffer_size, GFP_KERNEL);
	if (!buf) {
		DBG("kmalloc failed\n");
		return -ENOMEM;
	}

	r = display->driver->reg_read(display, reg_read->address,
				reg_read->buffer_size, buf,
				reg_read->use_hs_mode);
	if ((r == 0) &&
		(copy_to_user(reg_read->buffer, buf, reg_read->buffer_size)))
		r = -EFAULT;

	kfree(buf);

	return r;
}

static int omap_fbdev_reg_write(struct omap_dss_device *display,
				struct omapfb_reg_access *reg_write)
{
	int r = 0;
	u8 *buf;

	if (display == NULL)
		return -ENODEV;

	if (!display->driver->reg_write)
		return -EINVAL;

	/* +1 for register byte */
	buf = kmalloc(reg_write->buffer_size + 1, GFP_KERNEL);
	if (!buf) {
		DBG("kmalloc failed\n");
		return -ENOMEM;
	}

	/* Set the address as the first byte */
	buf[0] = reg_write->address;
	if (copy_from_user(&buf[1],
			reg_write->buffer,
			reg_write->buffer_size))
		r = -EFAULT;
	else
		r = display->driver->reg_write(display,
					reg_write->buffer_size + 1, buf,
					reg_write->use_hs_mode);

	kfree(buf);

	return r;
}
#  endif /* CONFIG_FB_OMAP2_REG_IOCTL */

/* mainly for emulating omapfb custom ioctl's */
static int omap_fbdev_ioctl(struct fb_info *fbi, unsigned int cmd,
		unsigned long arg)
{
	struct drm_fb_helper *helper = get_fb(fbi);
	struct drm_device *dev = helper->dev;
	int r = 0;

	switch (cmd) {
#  ifdef CONFIG_FB_OMAP2_REG_IOCTL
	case OMAPFB_REG_READ: {
		struct drm_connector *connector = NULL;
		struct omapfb_reg_access ra;

		if (copy_from_user(&ra, (void __user *)arg, sizeof(ra))) {
			r = -EFAULT;
			break;
		}

		while ((connector = omap_fbdev_get_next_connector(fbi,
								connector))) {
			struct omap_dss_device *dssdev =
					omap_connector_get_device(connector);
			r = omap_fbdev_reg_read(dssdev, &ra);
		}

		break;
	}
	case OMAPFB_REG_WRITE: {
		struct drm_connector *connector = NULL;
		struct omapfb_reg_access ra;

		if (copy_from_user(&ra, (void __user *)arg, sizeof(ra))) {
			r = -EFAULT;
			break;
		}

		while ((connector = omap_fbdev_get_next_connector(fbi,
								connector))) {
			struct omap_dss_device *dssdev =
					omap_connector_get_device(connector);
			r = omap_fbdev_reg_write(dssdev, &ra);
		}

		break;
	}
#  endif /* CONFIG_FB_OMAP2_REG_IOCTL */
	default:
		dev_err(dev->dev, "Unknown ioctl: 0x%08x\n", cmd);
		r = -EINVAL;
	}

	return r;
}
#else
#  define omap_fbdev_ioctl NULL
#endif /* CONFIG_DRM_OMAP_FB_OMAP2_COMPAT */

static struct fb_ops omap_fb_ops = {
	.owner = THIS_MODULE,

	/* Note: to properly handle manual update displays, we wrap the
	 * basic fbdev ops which write to the framebuffer
	 */
	.fb_read = fb_sys_read,
	.fb_write = omap_fbdev_write,
	.fb_fillrect = omap_fbdev_fillrect,
	.fb_copyarea = omap_fbdev_copyarea,
	.fb_imageblit = omap_fbdev_imageblit,

	.fb_check_var = drm_fb_helper_check_var,
	.fb_set_par = drm_fb_helper_set_par,
	.fb_pan_display = drm_fb_helper_pan_display,
	.fb_blank = drm_fb_helper_blank,
	.fb_setcmap = drm_fb_helper_setcmap,
	.fb_ioctl = omap_fbdev_ioctl,
};

static void omap_fbdev_deferred_io(struct fb_info *fbi,
			    struct list_head *pagelist)
{
	struct page *cur;

	/* walk the written page list and flush the data */
	list_for_each_entry(cur, pagelist, lru) {
		void *va = page_address(cur);
		unsigned long pa = page_to_phys(cur);
		dmac_flush_range(va, va + PAGE_SIZE);
		outer_flush_range(pa, pa + PAGE_SIZE);
	}

	/* someone has written to mmap'd framebuffer from userspace, and the
	 * timeout has elapsed.. so flush the fb to properly support manual
	 * update displays
	 */
	omap_fbdev_flush(fbi, 0, 0, fbi->var.xres, fbi->var.yres);
}

struct fb_deferred_io omap_fbdev_defio = {
	.delay		= HZ / 30,
	.deferred_io	= omap_fbdev_deferred_io,
};

static int omap_fbdev_create(struct drm_fb_helper *helper,
		struct drm_fb_helper_surface_size *sizes)
{
	struct omap_fbdev *fbdev = to_omap_fbdev(helper);
	struct drm_device *dev = helper->dev;
	struct fb_info *fbi;
	struct drm_mode_fb_cmd mode_cmd = {0};
	struct device *device = &dev->platformdev->dev;
	int ret;

	/* only doing ARGB32 since this is what is needed to alpha-blend
	 * with video overlays:
	 */
	sizes->surface_bpp = 32;
	sizes->surface_depth = 32;

	DBG("create fbdev: %dx%d@%d", sizes->surface_width,
			sizes->surface_height, sizes->surface_bpp);

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;

	mode_cmd.bpp = sizes->surface_bpp;
	mode_cmd.depth = sizes->surface_depth;

	mutex_lock(&dev->struct_mutex);

	fbi = framebuffer_alloc(0, device);
	if (!fbi) {
		dev_err(dev->dev, "failed to allocate fb info\n");
		ret = -ENOMEM;
		goto fail;
	}

	DBG("fbi=%p, dev=%p", fbi, dev);

	fbdev->fb = omap_framebuffer_init(dev, &mode_cmd);
	if (!fbdev->fb) {
		dev_err(dev->dev, "failed to allocate fb\n");
		ret = -ENOMEM;
		goto fail;
	}

	helper->fb = fbdev->fb;
	helper->fbdev = fbi;

	fbi->par = helper;
	fbi->flags = FBINFO_DEFAULT;
	fbi->fbops = &omap_fb_ops;

	fbi->fbdefio = &omap_fbdev_defio;
	fb_deferred_io_init(fbi);

	/* TODO: if we want to support making this driver a module, we
	 * need to call fb_deferred_io_cleanup() when the fbdev is
	 * unregistered!
	 */

	strcpy(fbi->fix.id, MODULE_NAME);

	ret = fb_alloc_cmap(&fbi->cmap, 256, 0);
	if (ret) {
		ret = -ENOMEM;
		goto fail;
	}

	omap_fbdev_update(helper, fbdev->fb);

	DBG("par=%p, %dx%d", fbi->par, fbi->var.xres, fbi->var.yres);
	DBG("allocated %dx%d fb", fbdev->fb->width, fbdev->fb->height);

	mutex_unlock(&dev->struct_mutex);

	return 0;

fail:
	mutex_unlock(&dev->struct_mutex);
	// TODO cleanup?
	return ret;
}

static void omap_crtc_fb_gamma_set(struct drm_crtc *crtc,
		u16 red, u16 green, u16 blue, int regno)
{
	DBG("fbdev: set gamma");
	// XXX ignore?
}

static void omap_crtc_fb_gamma_get(struct drm_crtc *crtc,
		u16 *red, u16 *green, u16 *blue, int regno)
{
	DBG("fbdev: get gamma");
	// XXX ignore?
}

static int omap_fbdev_probe(struct drm_fb_helper *helper,
		struct drm_fb_helper_surface_size *sizes)
{
	int new_fb = 0;
	int ret;

	if (!helper->fb) {
		ret = omap_fbdev_create(helper, sizes);
		if (ret)
			return ret;
		new_fb = 1;
	}
	return new_fb;
}

static struct drm_fb_helper_funcs omap_fb_helper_funcs = {
	.gamma_set = omap_crtc_fb_gamma_set,
	.gamma_get = omap_crtc_fb_gamma_get,
	.fb_probe = omap_fbdev_probe,
};

void omap_fbdev_update(struct drm_fb_helper *helper,
		struct drm_framebuffer *fb)
{
	struct fb_info *fbi = helper->fbdev;
	struct drm_device *dev = helper->dev;
	struct omap_fbdev *fbdev = to_omap_fbdev(helper);
	unsigned long paddr;
	void __iomem *vaddr;
	int size, screen_width;

	DBG("update fbdev: %dx%d, fbi=%p", fb->width, fb->height, fbi);

	fbdev->fb = fb;

	drm_fb_helper_fill_fix(fbi, fb->pitch, fb->depth);
	drm_fb_helper_fill_var(fbi, helper, fb->width, fb->height);

	size = omap_framebuffer_get_buffer(fbdev->fb, 0, 0,
			&vaddr, &paddr, &screen_width);

	dev->mode_config.fb_base = paddr;

	fbi->screen_base = vaddr;
	fbi->screen_size = size;
	fbi->fix.smem_start = paddr;
	fbi->fix.smem_len = size;
}

struct drm_connector * omap_fbdev_get_next_connector(struct fb_info *fbi,
		struct drm_connector *from)
{
	struct drm_fb_helper *helper = get_fb(fbi);

	if (!helper)
		return NULL;

	return omap_framebuffer_get_next_connector(helper->fb, from);
}
EXPORT_SYMBOL(omap_fbdev_get_next_connector);

/* flush an area of the framebuffer (in case of manual update display that
 * is not automatically flushed)
 */
void omap_fbdev_flush(struct fb_info *fbi, int x, int y, int w, int h)
{
	struct drm_fb_helper *helper = get_fb(fbi);

	if (!helper)
		return;

	DBG("flush fbdev: %d,%d %dx%d, fbi=%p", x, y, w, h, fbi);

	omap_framebuffer_flush(helper->fb, x, y, w, h);
}
EXPORT_SYMBOL(omap_fbdev_flush);

/* initialize fbdev helper */
struct drm_fb_helper * omap_fbdev_init(struct drm_device *dev)
{
	struct omap_gpu_private *priv = dev->dev_private;
	struct omap_fbdev *fbdev = NULL;
	struct drm_fb_helper *helper;
	int ret = 0;

	fbdev = kzalloc(sizeof(*fbdev), GFP_KERNEL);
	if (!fbdev) {
		dev_err(dev->dev, "could not allocate fbdev\n");
		goto fail;
	}

	helper = &fbdev->base;

	helper->funcs = &omap_fb_helper_funcs;

	ret = drm_fb_helper_init(dev, helper, priv->num_crtcs, 4);
	if (ret) {
		dev_err(dev->dev, "could not init fbdev: ret=%d\n", ret);
		goto fail;
	}

	drm_fb_helper_single_add_all_connectors(helper);
	drm_fb_helper_initial_config(helper, 32);

	priv->fbdev = helper;

	return helper;

fail:
	if (fbdev) {
		kfree(fbdev);
	}
	return NULL;
}
