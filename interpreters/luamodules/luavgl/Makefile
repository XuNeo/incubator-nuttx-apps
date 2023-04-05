############################################################################
# apps/interpreters/luamodules/luavgl/Makefile
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.  The
# ASF licenses this file to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance with the
# License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations
# under the License.
#
############################################################################

include $(APPDIR)/Make.defs

# luavgl library

LUAVGL_VERSION  = $(patsubst "%",%,$(strip $(CONFIG_LUA_LUAVGL_VERSION)))
LUAVGL_TARBALL  = v$(LUAVGL_VERSION).tar.gz
LUAVGL_UNPACK   = luavgl
LUAVGL_URL_BASE = https://github.com/XuNeo/luavgl/archive/refs/tags/
LUAVGL_URL      = $(LUAVGL_URL_BASE)/$(LUAVGL_TARBALL)

VPATH += $(LUAVGL_UNPACK)/src
CSRCS = luavgl.c
CFLAGS += ${INCDIR_PREFIX}$(LUAVGL_UNPACK)/src/
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/graphics/lvgl/lvgl

# luavgl download and unpack

$(LUAVGL_TARBALL):
	$(Q) echo "Downloading $(LUAVGL_TARBALL)"
	$(Q) curl -O -L $(LUAVGL_URL)
	$(Q) echo "Unpacking $(LUAVGL_TARBALL) to $(LUAVGL_UNPACK)"
	$(Q) tar -xvzf $(LUAVGL_TARBALL)
	$(Q) mv luavgl-$(LUAVGL_VERSION) $(LUAVGL_UNPACK)

$(LUAVGL_UNPACK)/.patch: $(LUAVGL_TARBALL)
	touch $(LUAVGL_UNPACK)/.patch

# Download and unpack tarball if no git repo found
ifeq ($(wildcard $(LUAVGL_UNPACK)/.git),)
distclean::
	$(call DELDIR, $(LUAVGL_UNPACK))
	$(call DELFILE, $(LUAVGL_TARBALL))

context:: $(LUAVGL_UNPACK)/.patch
endif

# Set LUAMODNAME and include Module.mk to add this module to the list of
# builtin modules for the Lua interpreter. LUAMODNAME should match the
# module's luaopen function.

LUAMODNAME = lvgl

include $(APPDIR)/interpreters/lua/Module.mk

include $(APPDIR)/Application.mk
