EXTRA_DIST = config.rpath  \
		COPYRIGHT \
		ChangeLog.API \
		ChangeLog.win32 \
		Doxyfile.in \
		HACKING \
		Makefile.mingw \
		PLUGIN_HOWTO \
		README.mingw \
		config.h.mingw \
		doxy2devhelp.xsl \
		fix-casts.sh \
		gaim.pc.in \
		gaim-uninstalled.pc.in \
		intltool-extract.in \
		intltool-merge.in \
		intltool-update.in \
		package_revision.h \
		pidgin.apspec.in \
		pidgin.spec.in \
		po/Makefile.mingw \
		valgrind-suppressions

noinst_HEADERS = config.h package_revision.h

dist-hook: pidgin.spec
	cp pidgin.spec $(distdir)
	rm $(distdir)/config.h

distcheck-hook: libpurple/plugins/perl/common/Purple.pm pidgin/plugins/perl/common/Pidgin.pm
#	cp libpurple/plugins/perl/common/Gaim.pm $(distdir)/libpurple/plugins/perl/common

commit-check:
	(cd po ; intltool-update -m 2>&1 | grep -v '^mismatched quotes.*\.py$$' | sed "s|current directory|po directory|" | grep . ; if [ $$? = 0 ] ; then exit 1 ; else exit 0 ; fi)

# Ensure these files are sorted and contain no duplicates:
	LC_ALL=C sort -c -t/ -u po/POTFILES.in
	LC_ALL=C sort -c -t/ -u po/POTFILES.skip

# Ensure COPYRIGHT is 100% UTF-8
	iconv -f utf8 -t utf8 COPYRIGHT | cmp COPYRIGHT -

version-check:
# Ensure our version string does not contain "dev"
	test x`echo $(PACKAGE_VERSION) | grep dev` = x

# Ensure ChangeLogs have the correct version
	head ChangeLog     | grep "^version $(PACKAGE_VERSION) (.*):$$" >/dev/null
	head ChangeLog.API | grep "^version $(PACKAGE_VERSION):$$" >/dev/null

# Ensure we're working from a tag...
	test x`hg log -r "tag(v$(PACKAGE_VERSION))" --template "{node}"` = x`hg log -r . -T '{node}'`
# ... and have no changes in the working copy.
	test "x`hg st -mard`" = x

sign-packages: dist
	gpg -ab pidgin-$(PACKAGE_VERSION).tar.gz
	gpg -ab pidgin-$(PACKAGE_VERSION).tar.bz2
	gpg --verify pidgin-$(PACKAGE_VERSION).tar.gz.asc pidgin-$(PACKAGE_VERSION).tar.gz
	gpg --verify pidgin-$(PACKAGE_VERSION).tar.bz2.asc pidgin-$(PACKAGE_VERSION).tar.bz2

release: commit-check version-check distcheck sign-packages

if INSTALL_I18N
PO_DIR=po
endif

if ENABLE_GTK
GTK_DIR=pidgin
endif

if ENABLE_GNT
GNT_DIR=finch
endif

# We always try to rebuild all objects that depends on this dummy target.
.FORCE:

# if both attempts fail, then we need to remove the empty file that >
# creates, and also make sure that the shell command exits
# successfully; the rm -f ensures both
package_revision_raw.txt: .FORCE
	$(AM_V_GEN)REAL_BLDDIR=$$PWD/$(top_builddir); \
	(hg --cwd $(srcdir) log -r . -T {node}) 2>/dev/null >$@.new \
	|| rm -f $@.new
	$(AM_V_at)if test -f $@.new; then \
		if ! diff $@ $@.new > /dev/null; then \
			mv $@.new $@; \
		else \
			rm $@.new; \
		fi \
	fi
package_revision.h: package_revision_raw.txt
	$(AM_V_GEN)if test -f $<; then \
		echo "#define REVISION \"`cat $<`\"" > $@; \
	fi
	$(AM_V_at)if test ! -f $@ -a -f $(srcdir)/$@; then \
		cp $(srcdir)/$@ $@; \
	fi
	$(AM_V_at)test -f $@ || echo "#define REVISION \"unknown\"" > $@

# This is a magic directive copy-and-pasted, then modified, from the
# automake 1.9 manual, section 13.4, "Checking the distribution".
# Normally, 'distcheck' does a clean build, and then afterwards runs
# 'distclean', and 'distclean' is supposed to remove everything that
# the build created.  However, we have some targets (package_revision.txt)
# that we distribute, but then always attempt to rebuild optimistically, and
# then if that fails fall back on the distributed versions.  This
# means that 'distclean' should _not_ remove those files, since they
# are distributed, yet building the package will generate those files,
# thus automake thinks that 'distclean' _should_ remove those files,
# and 'distcheck' gets cranky if we don't.  So basically what this
# line does is tell 'distcheck' to shut up and ignore those two files.
distcleancheck_listfiles = find . -type f -a ! -name package_revision.h

SUBDIRS = . libpurple doc $(GNT_DIR) $(GTK_DIR) m4macros $(PO_DIR) share/ca-certs share/sounds

docs: Doxyfile
if HAVE_DOXYGEN
	@echo "Running doxygen..."
	@doxygen
if HAVE_XSLTPROC
	@echo "Generating devhelp index..."
	@xsltproc $(top_srcdir)/doxy2devhelp.xsl doc/xml/index.xml > doc/html/pidgin.devhelp
	@echo "(Symlink $$(pwd)/doc/html to ~/.local/share/gtk-doc/html/pidgin to make devhelp see the documentation)"
else
	@echo "Not generating devhelp index: xsltproc was not found by configure"
endif
else
	@echo "doxygen was not found during configure.  Unable to build documentation."
	@echo;
endif

# perl's MakeMaker uninstall foo doesn't work well with DESTDIR set, which
# breaks "make distcheck" unless we ignore perl things
# TODO drop it when we drop perl wrapper

distuninstallcheck_listfiles = \
	find . -type f -print | grep -v perl | grep -v Purple.3pm | grep -v Pidgin.3pm

DISTCLEANFILES= intltool-extract intltool-merge intltool-update \
			package_revision_raw.txt

# This file isn't cleaned up due to an upstream intltool bug
# https://bugs.launchpad.net/intltool/+bug/1712194
DISTCLEANFILES += po/.intltool-merge-cache.lock

ACLOCAL_AMFLAGS = -I m4
