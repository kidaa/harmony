# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.
# The ASF licenses this file to You under the Apache License, Version 2.0
# (the "License"); you may not use this file except in compliance with
# the License.  You may obtain a copy of the License at
# 
#     http://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

#
# Makefile for module 'zip'
#

HY_BIN=$(HY_TARGET)/build/classlib/archive/native/zip/
include $(HY_TARGET)/hdk/build/make/defines.mk
ZLIB_DIST = $(HY_TARGET)/depends/native/zlib_dist/
INCLUDES += -I$(ZLIB_DIST)

BUILDFILES = zcpool.o zipalloc.o zipcache.o zipsup.o pool.o pool_cap.o

ifeq ($(HY_LOCAL_ZLIB),true)
DEFINES += -DHY_LOCAL_ZLIB
else

ifeq ($(HY_OS),zos)
MDLLIBZLIB += $(LIBPATH)libhyzlib$(HY_LINKLIB_SUFFIX)
else
MDLLIBZLIB += $(DLLPATH)libhyzlib$(HY_LINKLIB_SUFFIX)
endif

endif

LIBNAME = $(LIBPATH)libhyzip.a

$(HY_BIN)%.o: $(LIBPATH)libhypool.a(%.o)
	( cd $(HY_BIN) && ar $(ARFLAGS) $(AREXTRACT) $(LIBPATH)libhypool.a $@ )

include $(HY_TARGET)/hdk/build/make/rules.mk
