# -*- Mode: makefile -*-
# 
# The contents of this file are subject to the Netscape Public
# License Version 1.1 (the "License"); you may not use this file
# except in compliance with the License. You may obtain a copy of
# the License at http://www.mozilla.org/NPL/
# 
# Software distributed under the License is distributed on an "AS
# IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
# implied. See the License for the specific language governing
# rights and limitations under the License.
# 
# The Original Code is Mozilla Communicator client code, released
# March 31, 1998.
# 
# The Initial Developer of the Original Code is Netscape
# Communications Corporation. Portions created by Netscape are
# Copyright (C) 1998-1999 Netscape Communications Corporation. All
# Rights Reserved.
# 
# Contributor(s):
#       Michael Ang <mang@subcarrier.org>
# 
# Alternatively, the contents of this file may be used under the
# terms of the GNU Public License (the "GPL"), in which case the
# provisions of the GPL are applicable instead of those above.
# If you wish to allow use of your version of this file only
# under the terms of the GPL and not to allow others to use your
# version of this file under the NPL, indicate your decision by
# deleting the provisions above and replace them with the notice
# and other provisions required by the GPL.  If you do not delete
# the provisions above, a recipient may use your version of this
# file under either the NPL or the GPL.
# 

#
# JSRef GNUmake makefile rules
#

ifdef USE_MSVC
LIB_OBJS  = $(addprefix $(OBJDIR)/, $(LIB_CFILES:.c=.obj))
PROG_OBJS = $(addprefix $(OBJDIR)/, $(PROG_CFILES:.c=.obj))
else
LIB_OBJS  = $(addprefix $(OBJDIR)/, $(LIB_CFILES:.c=.o))
LIB_OBJS  += $(addprefix $(OBJDIR)/, $(LIB_ASFILES:.s=.o))
PROG_OBJS = $(addprefix $(OBJDIR)/, $(PROG_CFILES:.c=.o))
endif

CFILES = $(LIB_CFILES) $(PROG_CFILES)
OBJS   = $(LIB_OBJS) $(PROG_OBJS)

ifdef USE_MSVC
# TARGETS = $(LIBRARY)   # $(PROGRAM) not supported for MSVC yet
TARGETS += $(SHARED_LIBRARY) $(PROGRAM)  # it is now
else
TARGETS += $(LIBRARY) $(SHARED_LIBRARY) $(PROGRAM) 
endif

all:
	+$(LOOP_OVER_PREDIRS) 
ifneq "$(strip $(TARGETS))" ""
	$(MAKE) -f Makefile.ref $(TARGETS)
endif
	+$(LOOP_OVER_DIRS)

$(OBJDIR)/%: %.c
	@$(MAKE_OBJDIR)
	$(CC) -o $@ $(CFLAGS) $*.c $(LDFLAGS)

# This rule must come before the rule with no dep on header
$(OBJDIR)/%.o: %.c %.h
	@$(MAKE_OBJDIR)
	$(CC) -o $@ -c $(CFLAGS) $*.c


$(OBJDIR)/%.o: %.c
	@$(MAKE_OBJDIR)
	$(CC) -o $@ -c $(CFLAGS) $*.c

$(OBJDIR)/%.o: %.s
	@$(MAKE_OBJDIR)
	$(AS) -o $@ $(ASFLAGS) $*.s

# This rule must come before rule with no dep on header
$(OBJDIR)/%.obj: %.c %.h
	@$(MAKE_OBJDIR)
	$(CC) -Fo$(OBJDIR)/ -c $(CFLAGS) $(JSDLL_CFLAGS) $*.c

$(OBJDIR)/%.obj: %.c
	@$(MAKE_OBJDIR)
	$(CC) -Fo$(OBJDIR)/ -c $(CFLAGS) $(JSDLL_CFLAGS) $*.c

$(OBJDIR)/js.obj: js.c
	@$(MAKE_OBJDIR)
	$(CC) -Fo$(OBJDIR)/ -c $(CFLAGS) $<

ifeq ($(OS_ARCH),OS2)
$(LIBRARY): $(LIB_OBJS)
	$(AR) $@ $? $(AR_OS2_SUFFIX)
	$(RANLIB) $@
else
ifdef USE_MSVC
$(SHARED_LIBRARY): $(LIB_OBJS)
	link.exe $(LIB_LINK_FLAGS) /base:0x61000000 $(OTHER_LIBS) \
	    /out:"$@" /pdb:none\
	    /implib:"$(OBJDIR)/$(@F:.dll=.lib)" $^
else
$(LIBRARY): $(LIB_OBJS)
	$(AR) rv $@ $?
	$(RANLIB) $@

$(SHARED_LIBRARY): $(LIB_OBJS)
	$(MKSHLIB) -o $@ $(LIB_OBJS) $(LDFLAGS) $(OTHER_LIBS)
endif
endif

# Java stuff
$(CLASSDIR)/$(OBJDIR)/$(JARPATH)/%.class: %.java
	mkdir -p $(@D)
	$(JAVAC) $(JAVAC_FLAGS) $<

define MAKE_OBJDIR
if test ! -d $(@D); then rm -rf $(@D); mkdir -p $(@D); fi
endef

ifdef DIRS
LOOP_OVER_DIRS		=					\
	@for d in $(DIRS); do					\
		if test -d $$d; then				\
			set -e;			\
			echo "cd $$d; $(MAKE) -f Makefile.ref $@"; 		\
			cd $$d; $(MAKE) -f Makefile.ref $@; cd ..;	\
			set +e;					\
		else						\
			echo "Skipping non-directory $$d...";	\
		fi;						\
	done
endif

ifdef PREDIRS
LOOP_OVER_PREDIRS	=					\
	@for d in $(PREDIRS); do				\
		if test -d $$d; then				\
			set -e;			\
			echo "cd $$d; $(MAKE) -f Makefile.ref $@"; 		\
			cd $$d; $(MAKE) -f Makefile.ref $@; cd ..;	\
			set +e;					\
		else						\
			echo "Skipping non-directory $$d...";	\
		fi;						\
	done
endif

export:
	+$(LOOP_OVER_PREDIRS)	
	mkdir -p $(DIST)/include $(DIST)/$(LIB) $(DIST)/bin
ifneq "$(strip $(HFILES))" ""
	$(CP) $(HFILES) $(DIST)/include
endif
ifneq "$(strip $(LIBRARY))" ""
	$(CP) $(LIBRARY) $(DIST)/$(LIB)
endif
ifneq "$(strip $(JARS))" ""
	$(CP) $(JARS) $(DIST)/$(LIB)
endif
ifneq "$(strip $(SHARED_LIBRARY))" ""
	$(CP) $(SHARED_LIBRARY) $(DIST)/$(LIB)
endif
ifneq "$(strip $(PROGRAM))" ""
	$(CP) $(PROGRAM) $(DIST)/bin
endif
	+$(LOOP_OVER_DIRS)

clean:
	rm -rf $(OBJS)
	@cd fdlibm; $(MAKE) -f Makefile.ref clean

clobber:
	rm -rf $(OBJS) $(TARGETS) $(DEPENDENCIES)
	@cd fdlibm; $(MAKE) -f Makefile.ref clobber

depend:
	gcc -MM $(CFLAGS) $(LIB_CFILES)

tar:
	tar cvf $(TARNAME) $(TARFILES)
	gzip $(TARNAME)

