SUBDIRS = lib intl src po man
EXTRA_DIST = @README_ALPHA@ check.lrzsz COMPATABILITY


check:
	QUIET=-q $(srcdir)/check.lrzsz $(srcdir) `pwd`	
vcheck:
	$(srcdir)/check.lrzsz $(srcdir) `pwd`	

# Tag before making distribution.  Also, don't make a distribution if
# checks fail.  Also, make sure the NEWS file is up-to-date.
cvs-dist: 
	@if sed 1q NEWS | grep -e "$(VERSION)" > /dev/null; then :; else \
		echo "NEWS not updated; not releasing" 1>&2; \
		exit 1;                               \
	fi
	cvs tag `echo "Release-$(VERSION)" | sed 's/\./-/g'`
	$(MAKE) dist


