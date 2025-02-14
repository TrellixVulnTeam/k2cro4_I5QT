SRC_DIR := $(SRC_PATH)/lib$(NAME)

include $(SRC_PATH)/common.mak

LIBVERSION := $(lib$(NAME)_VERSION)
LIBMAJOR   := $(lib$(NAME)_VERSION_MAJOR)
INCINSTDIR := $(INCDIR)/lib$(NAME)
THIS_LIB   := $(SUBDIR)$($(CONFIG_SHARED:yes=S)LIBNAME)

all-$(CONFIG_STATIC): $(SUBDIR)$(LIBNAME)
all-$(CONFIG_SHARED): $(SUBDIR)$(SLIBNAME)

$(SUBDIR)%-test.o: $(SUBDIR)%-test.c
	$(COMPILE_C)

$(SUBDIR)%-test.o: $(SUBDIR)%.c
	$(COMPILE_C)

$(SUBDIR)x86/%.o: $(SUBDIR)x86/%.asm
	$(DEPYASM) $(YASMFLAGS) -I $(<D)/ -M -o $@ $< > $(@:.o=.d)
	$(YASM) $(YASMFLAGS) -I $(<D)/ -o $@ $<

$(OBJS): CPPFLAGS := -DCOMPILING_$(NAME)=1 $(CPPFLAGS)
$(OBJS) $(OBJS:.o=.s) $(SUBDIR)%.h.o $(TESTOBJS): CPPFLAGS += -DHAVE_AV_CONFIG_H
$(TESTOBJS): CPPFLAGS += -DTEST

$(SUBDIR)$(LIBNAME): $(OBJS)
	$(RM) $@
	$(AR) $(ARFLAGS) $(AR_O) $^ $(EXTRAOBJS)
	$(RANLIB) $@

install-headers: install-lib$(NAME)-headers install-lib$(NAME)-pkgconfig

install-libs-$(CONFIG_STATIC): install-lib$(NAME)-static
install-libs-$(CONFIG_SHARED): install-lib$(NAME)-shared

define RULES
$(EXAMPLES) $(TESTPROGS) $(TOOLS): %$(EXESUF): %.o
	$$(LD) $(LDFLAGS) $$(LD_O) $$^ $(FULLNAME:%=$(LD_LIB)) $(FFEXTRALIBS) $$(ELIBS)

$(SUBDIR)$(SLIBNAME): $(SUBDIR)$(SLIBNAME_WITH_MAJOR)
	$(Q)cd ./$(SUBDIR) && $(LN_S) $(SLIBNAME_WITH_MAJOR) $(SLIBNAME)

$(SUBDIR)$(SLIBNAME_WITH_MAJOR): $(OBJS) $(SUBDIR)lib$(NAME).ver
	$(SLIB_CREATE_DEF_CMD)
	$$(LD) $(SHFLAGS) $(LDFLAGS) $$(LD_O) $$(filter %.o,$$^) $(FFEXTRALIBS) $(EXTRAOBJS)
	$(SLIB_EXTRA_CMD)

ifdef SUBDIR
$(SUBDIR)$(SLIBNAME_WITH_MAJOR): $(DEP_LIBS)
endif

clean::
	$(RM) $(addprefix $(SUBDIR),*-example$(EXESUF) *-test$(EXESUF) $(CLEANFILES) $(CLEANSUFFIXES) $(LIBSUFFIXES)) \
	    $(CLEANSUFFIXES:%=$(SUBDIR)$(ARCH)/%) $(HOSTOBJS) $(HOSTPROGS)

distclean:: clean
	$(RM) $(DISTCLEANSUFFIXES:%=$(SUBDIR)%) $(DISTCLEANSUFFIXES:%=$(SUBDIR)$(ARCH)/%)

install-lib$(NAME)-shared: $(SUBDIR)$(SLIBNAME)
	$(Q)mkdir -p "$(SHLIBDIR)"
	$$(INSTALL) -m 755 $$< "$(SHLIBDIR)/$(SLIB_INSTALL_NAME)"
	$$(STRIP) "$(SHLIBDIR)/$(SLIB_INSTALL_NAME)"
	$(Q)$(foreach F,$(SLIB_INSTALL_LINKS),cd "$(SHLIBDIR)" && $(LN_S) $(SLIB_INSTALL_NAME) $(F);)
	$(if $(SLIB_INSTALL_EXTRA_SHLIB),$$(INSTALL) -m 644 $(SLIB_INSTALL_EXTRA_SHLIB:%=$(SUBDIR)%) "$(SHLIBDIR)")
	$(if $(SLIB_INSTALL_EXTRA_LIB),$(Q)mkdir -p "$(LIBDIR)")
	$(if $(SLIB_INSTALL_EXTRA_LIB),$$(INSTALL) -m 644 $(SLIB_INSTALL_EXTRA_LIB:%=$(SUBDIR)%) "$(LIBDIR)")

install-lib$(NAME)-static: $(SUBDIR)$(LIBNAME)
	$(Q)mkdir -p "$(LIBDIR)"
	$$(INSTALL) -m 644 $$< "$(LIBDIR)"
	$(LIB_INSTALL_EXTRA_CMD)

install-lib$(NAME)-headers: $(addprefix $(SUBDIR),$(HEADERS) $(BUILT_HEADERS))
	$(Q)mkdir -p "$(INCINSTDIR)"
	$$(INSTALL) -m 644 $$^ "$(INCINSTDIR)"

install-lib$(NAME)-pkgconfig: $(SUBDIR)lib$(NAME).pc
	$(Q)mkdir -p "$(LIBDIR)/pkgconfig"
	$$(INSTALL) -m 644 $$^ "$(LIBDIR)/pkgconfig"

uninstall-libs::
	-$(RM) "$(SHLIBDIR)/$(SLIBNAME_WITH_MAJOR)" \
	       "$(SHLIBDIR)/$(SLIBNAME)"            \
	       "$(SHLIBDIR)/$(SLIBNAME_WITH_VERSION)"
	-$(RM)  $(SLIB_INSTALL_EXTRA_SHLIB:%="$(SHLIBDIR)/%")
	-$(RM)  $(SLIB_INSTALL_EXTRA_LIB:%="$(LIBDIR)/%")
	-$(RM) "$(LIBDIR)/$(LIBNAME)"

uninstall-headers::
	$(RM) $(addprefix "$(INCINSTDIR)/",$(HEADERS) $(BUILT_HEADERS))
	$(RM) "$(LIBDIR)/pkgconfig/lib$(NAME).pc"
	-rmdir "$(INCINSTDIR)"
endef

$(eval $(RULES))

#$(EXAMPLES) $(TESTPROGS) $(TOOLS): $(THIS_LIB) $(DEP_LIBS)
$(TESTPROGS): $(SUBDIR)$(LIBNAME)

examples: $(EXAMPLES)
testprogs: $(TESTPROGS)
