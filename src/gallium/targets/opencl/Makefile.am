include $(top_srcdir)/src/gallium/Automake.inc

lib_LTLIBRARIES = lib@OPENCL_LIBNAME@.la

lib@OPENCL_LIBNAME@_la_LDFLAGS = \
	$(LLVM_LDFLAGS) \
	-no-undefined \
	-version-number @OPENCL_VERSION@:0 \
	$(GC_SECTIONS) \
	$(LD_NO_UNDEFINED)

if HAVE_LD_VERSION_SCRIPT
lib@OPENCL_LIBNAME@_la_LDFLAGS += \
	-Wl,--version-script=$(top_srcdir)/src/gallium/targets/opencl/opencl.sym
endif

lib@OPENCL_LIBNAME@_la_LIBADD = \
	$(top_builddir)/src/gallium/auxiliary/pipe-loader/libpipe_loader.la \
	$(top_builddir)/src/gallium/state_trackers/clover/libclover.la \
	$(top_builddir)/src/gallium/auxiliary/libgallium.la \
	$(top_builddir)/src/util/libmesautil.la \
	$(GALLIUM_PIPE_LOADER_WINSYS_LIBS) \
	$(ELF_LIB) \
	-ldl \
	-lclangCodeGen \
	-lclangFrontendTool \
	-lclangFrontend \
	-lclangDriver \
	-lclangSerialization \
	-lclangCodeGen \
	-lclangParse \
	-lclangSema \
	-lclangAnalysis \
	-lclangAST \
	-lclangEdit \
	-lclangLex \
	-lclangBasic \
	$(LLVM_LIBS) \
	$(PTHREAD_LIBS)

nodist_EXTRA_lib@OPENCL_LIBNAME@_la_SOURCES = dummy.cpp
lib@OPENCL_LIBNAME@_la_SOURCES =

EXTRA_lib@OPENCL_LIBNAME@_la_DEPENDENCIES = opencl.sym
EXTRA_DIST = mesa.icd opencl.sym

if HAVE_CLOVER_ICD
icddir = /etc/OpenCL/vendors/
icd_DATA = mesa.icd
endif

include $(top_srcdir)/install-gallium-links.mk
