# shellcheck shell=sh
# Sourced by gui-test.sh and look.sh after common.sh: the image window
# opens centred on the top left corner of the page. For screenshots it is
# moved into view on the page only ($show_image; Broadway does not know,
# so it is moved back with $unmove before anything is clicked), and it is
# focused through its visible lower right corner ($focus_image).
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
# shellcheck disable=SC2034
show_image="eval:(() => { for (const c of document.querySelectorAll('canvas'))
  if ((Number(c.style.zIndex) || 0) === 0 && c.getBoundingClientRect().width > 300) {
    const r = c.getBoundingClientRect();
    c.style.transform = 'translate(' + (${PAS_IMAGE_X:-0} - r.left) + 'px,' + (-r.top) + 'px)' }
  return 1 })()"
# shellcheck disable=SC2034
unmove="eval:(() => { for (const c of document.querySelectorAll('canvas'))
  c.style.transform = ''; return 1 })()"
# shellcheck disable=SC2034
focus_image="key:Escape wait:300 click:40,82 wait:500"
