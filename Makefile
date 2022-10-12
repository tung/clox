##################################################
### Makefile for C Projects using GCC or Clang ###
##################################################

# Begin variable tracking; leave overrides out so they're picked up later.
override_vars := $(patsubst %=,%,$(filter %=,$(subst =,= ,$(MAKEOVERRIDES))))
begin_vars := $(sort $(filter-out $(override_vars),$(.VARIABLES)))

###################
### Directories ###
###################

# Location(s) of source and header files.
src_dir    = src/
header_dir = $(src_dir)

# Desired base directory for build outputs.
# Each mode makes its own sub-directory under this one.
build_dir = build/

# Ensure path values don't start with "./", but end with a '/'.
no_dot_slash_but_end_slash = $(patsubst ./%,%,$(1:/=)/)
override src_dir    := $(call no_dot_slash_but_end_slash,$(src_dir))
override header_dir := $(call no_dot_slash_but_end_slash,$(header_dir))
override build_dir  := $(call no_dot_slash_but_end_slash,$(build_dir))

# Auto-detected directory of this Makefile; used to run helper scripts.
makefile_prefix = $(dir $(firstword $(MAKEFILE_LIST)))

# Auto-detected current path as an absolute path;
# for informational purposes only.
cur_dir = $(abspath .)

###################
### Build Modes ###
###################

# List of all possible modes.
all_modes = debug release coverage

# Override mode to coverage if "gcovr" is the target.
ifeq ($(MAKECMDGOALS),gcovr)
  override MODE = coverage
  $(info MODE = coverage override for gcovr target.)
endif

# If no mode is set, build in debug mode by default.
ifeq ($(MODE),)
  override MODE = debug
  $(info Using default MODE = $(MODE).)
endif

# Allow only valid modes beyond this point.
ifeq ($(filter $(MODE),$(all_modes)),)
  $(error MODE must be one of: $(all_modes) (MODE = $(MODE)))
endif

# Build directories for all modes; for 'cleanall' rule when $(build_dir) is empty.
all_mode_dirs = $(all_modes:%=$(build_dir)%/)

# Build directory for the current mode.
mode_dir = $(build_dir)$(MODE)/

# "debug" mode settings.
ifeq ($(MODE),debug)
  CFLAGS     = -Wall -Wextra -O1 -g -fsanitize=address -fno-omit-frame-pointer
  LDFLAGS    = -g -fsanitize=address
  LDLIBS     =
  TESTLDLIBS =

# "release" mode settings.
else ifeq ($(MODE),release)
  CFLAGS     = -Wall -Wextra -O3
  LDFLAGS    =
  LDLIBS     =
  TESTLDLIBS =

# "coverage" mode settings.
else ifeq ($(MODE),coverage)
  CFLAGS     = -Wall -Wextra -O1 -g -fsanitize=address -fno-omit-frame-pointer --coverage
  LDFLAGS    = -g -fsanitize=address --coverage
  LDLIBS     =
  TESTLDLIBS =
  GCOVR      = gcovr -e "$(src_dir)acutest.h"
  report_dir = $(mode_dir)gcovr/
  report_loc = $(report_dir)report.html

# All valid modes should have a settings block above.
else
  $(error Unhandled MODE = $(MODE); valid values: $(all_modes)))
endif

###########################
### Sources and Outputs ###
###########################

# Main target configuration.
main_name   = clox
main_src    = $(src_dir)main.c
main_target = $(mode_dir)$(main_name)

# Tests configuration.
test_srcs = $(wildcard $(src_dir)*_test.c)
tests     = $(sort $(test_srcs:$(src_dir)%.c=$(mode_dir)%))

# All C source files.
c_srcs = $(wildcard $(src_dir)*.c)

# Source files to apply automatic formatting to.
format_srcs = $(sort $(test_srcs))

# Output sub-directories for the current mode's build directory.
objs_dir = $(mode_dir)objs/
deps_dir = $(mode_dir)deps/

# Object and dependency file destinations.
c_objs         = $(c_srcs:$(src_dir)%.c=$(objs_dir)%.o)
c_deps         = $(c_srcs:$(src_dir)%.c=$(deps_dir)%.d)
main_link_dep  = $(main_src:$(src_dir)%.c=$(deps_dir)%.link.d)
test_link_deps = $(test_srcs:$(src_dir)%.c=$(deps_dir)%.link.d)

# Files associated with missing source files (moved or deleted).
# Auto-deleted by auto-run 'cleanmissing' rule.
missing_objs = $(filter-out $(c_objs),$(wildcard $(objs_dir)*.o))
missing_deps = $(filter-out $(c_deps) $(main_link_dep) $(test_link_deps),$(wildcard $(deps_dir)*.d))

###############
### Helpers ###
###############

# Name patterns of task-only targets that never build anything.
task_only_target_pats = clean clean% format checkformat

# Helpers to check if dependency files are needed for this run.
current_goals    = $(if $(MAKECMDGOALS),$(MAKECMDGOALS),all)
should_make_deps = $(filter-out $(task_only_target_pats),$(current_goals))

# End variable tracking and gather names of all custom variables above this point.
end_vars := $(sort $(.VARIABLES))
var_names = $(filter-out begin_vars $(subst %,\%,$(begin_vars)),$(end_vars))

# Helper to add "./" prefix to a path that doesn't already start with "/", "./" or "../".
run_path = "$(if $(filter / ./ ../,$(firstword $(subst /,/ ,$(1)))),,./)$(1)"

####################
### Task Targets ###
####################

# Run 'all' target if no specific target was requested.
.DEFAULT_GOAL = all

# Build main target and all tests by default.
all: build buildtests

# Run main target.
.PHONY: run
run: build
	$(call run_path,$(main_target))

# Run all tests.
.PHONY: test
test: buildtests
	"$(makefile_prefix)runtests.bash" $(foreach t,$(tests),$(call run_path,$(t)))

# Build main target.
.PHONY: build
build: $(main_target)

# Build all tests.
.PHONY: buildtests
buildtests: $(tests)

# Delete build outputs and dependency files for the current mode.
.PHONY: clean
clean: cleandeps
	$(RM) "$(main_target)" $(tests:%="%") "$(mode_mk)" "$(objs_dir)"*.o

# Delete dependency files for the current mode.
.PHONY: cleandeps
cleandeps:
	$(RM) "$(deps_dir)"*.d

# Delete the whole $(build_dir) directory.
.PHONY: cleanall
cleanall:
ifeq ($(build_dir),)
#	If we're sitting in the build base, just delete the mode directories.
	$(RM) -r $(all_mode_dirs:%="%")
else
	$(RM) -r "$(build_dir)"
endif

# Delete files associated with missing source files (moved or deleted).
.PHONY: cleanmissing
cleanmissing:
ifneq ($(missing_objs),)
	$(RM) $(missing_objs:%="%")
endif
ifneq ($(missing_deps),)
	$(RM) $(missing_deps:%="%")
endif
	@exit

# Always run 'cleanmissing' target before anything else with the -include .PHONY trick.
-include cleanmissing

#############################
### Code Style Formatting ###
#############################

# Reformat format-picked source code.
.PHONY: format
format:
	clang-format -i $(format_srcs:%="%")

# Check that format-picked source code passes automatic formatting without changes.
.PHONY: checkformat
checkformat:
	clang-format --dry-run -Werror $(format_srcs:%="%")

# Check format-picked files to git commit pass automatic formatting without changes.
# Put 'make gitcheckformat' in .git/hooks/pre-commit to use as a pre-commit hook.
.PHONY: gitcheckformat
gitcheckformat:
	srcs=$$({ git diff --name-only --cached; for f in $(format_srcs:%="%"); do echo "$$f"; done } | sort | uniq -d); \
for s in $${srcs}; do git show :"$$s" | clang-format --dry-run -Werror --assume-filename="$$s" || exit 1; done

##########################################################
### Compiling, Linking and Auto-Generated Dependencies ###
##########################################################

# Link main target binary.
# The $^ dependency list is in the auto-generated $(main_link_dep) file.
$(main_target):
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# Link test binaries.
# The $^ dependency lists are in the auto-generated $(test_link_deps) files.
$(tests): %:
	$(CC) $(LDFLAGS) -o $@ $^ $(TESTLDLIBS)

# Ensure build directories exist for targets, objs and deps.
$(main_target) $(tests): | $(mode_dir)
$(c_objs): | $(objs_dir)
$(c_deps) $(main_link_dep) $(test_link_deps): | $(deps_dir)
$(mode_dir) $(objs_dir) $(deps_dir):
	mkdir -p $@

# Compile $(objs_dir)*.o objects from each $(src_dir)*.c source file.
$(c_objs): $(objs_dir)%.o: $(src_dir)%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

# Auto-generate $(deps_dir)*.d files from each $(src_dir)*.c source file.
# These files contain *.o compile dependencies that are -include'd by this Makefile.
$(c_deps): $(deps_dir)%.d: $(src_dir)%.c
	$(CPP) $(CPPFLAGS) -MM -MT $(@:$(deps_dir)%.d=$(objs_dir)%.o) -MT $@ -MF $@ $<

# Auto-generate dependecy rule for $(main_target).
# This scans the dependency files for *.o files to be linked into $(main_target).
$(main_link_dep): | $(c_deps)
	"$(makefile_prefix)linkrule.bash" "$@" "$(main_target)" "$(@:.link.d=.d)" "$(header_dir)"

# Auto-generate dependency rules for $(tests).
# This scans the dependency files for *.o files to be linked into each test.
$(test_link_deps): %.link.d: | $(c_deps)
	"$(makefile_prefix)linkrule.bash" "$@" "$(*:$(deps_dir)%=$(mode_dir)%)" "$(@:.link.d=.d)" "$(header_dir)"

# Avoid remaking dependency files if we're just cleaning up.
ifneq ($(should_make_deps),)
  # Include auto-generated dependency files.
  # $(c_deps) lists source files that each *.o file should be recompiled for.
  -include $(c_deps)
  # The link dependencies list *.o files that each target should be relinked for.
  -include $(main_link_dep)
  -include $(test_link_deps)
endif

#########################################################################
### Coverage Mode: Report lines, branches and functions run by tests. ###
#########################################################################

ifeq ($(MODE),coverage)

# Rerun tests with coverage instrumentation, generate report and show its URI.
.PHONY: gcovr
gcovr: cleancoverage
	$(MAKE) --no-print-directory -f $(firstword $(MAKEFILE_LIST)) MODE=coverage test
	$(MAKE) --no-print-directory -f $(firstword $(MAKEFILE_LIST)) MODE=coverage report

# Generate/update a coverage report and show its URI.
.PHONY: report
report: $(report_loc)
	@printf "Coverage report at: file://%s\n" "$(abspath $(report_loc))"

# Generate a coverage report from *.gcda stats and *.gcno files.
$(report_loc): $(objs_dir) $(wildcard $(objs_dir)*.gcda) | $(report_dir)
	$(GCOVR) --html-details "$(report_loc)" -r "$(src_dir)" "$(objs_dir)"

# Ensure the report directory exists for report generation.
$(report_dir): | $(mode_dir)
	mkdir -p $@

# Extend the "clean" target in coverage mode to delete coverage files.
clean: cleancoverage cleangcno

# Delete coverage files that can be recreated without recompiling.
.PHONY: cleancoverage
cleancoverage: cleanreport cleanstats

# Clean coverage report files by deleting its directory.
.PHONY: cleanreport
cleanreport:
	$(RM) -r "$(report_dir)"

# Delete coverage stats.
.PHONY: cleanstats
cleanstats:
	$(RM) "$(objs_dir)"*.gcda

# Delete *.gcno support files made when *.o files are compiled for coverage.
# Doesn't make much sense on its own; exists to extend the "clean" target in
# coverage mode.
.PHONY: cleangcno
cleangcno:
	$(RM) "$(objs_dir)"*.gcno

endif # ifeq ($(MODE),coverage)

######################################################################
### Change Tracking for Directories that appear in Build Artifacts ###
######################################################################
#
# Variables ending with "_dir" are saved when building in a mode for the
# first time.  Paths in these variables often end up in object debug
# data and dependency files, so if values change between builds, strange
# things might happen.
#
# This section prevents builds if dir values change like this.  This can
# be overriden with FORCE=1 alongside 'make cleandeps', but a warning
# will be shown for all future builds until 'make clean' is run.
#
# TLDR: Always make *to* a build dir *from* the same current directory,
#       and you can ignore this whole section.

# All variables whose name ends with "_dir".
dir_vars = $(filter %_dir,$(var_names))

# Makefile fragment with all dir_vars and their values at first-build time.
mode_mk = $(mode_dir)mode.mk

# Populate $(mode_mk) directory variables and values in $(dir_vars).
$(mode_mk): | $(mode_dir)
	printf " $(foreach dv,$(dir_vars),orig_$(dv) = $($(dv))\n)" > $@

# Detect changes to dir values, but only if we're not cleaning up.
ifneq ($(should_make_deps),)
  # Load orig_*_dir variables so we can detect changed dir values.
  -include $(mode_mk)

  # Only detect dir value changes if $(mode_mk) existed to begin with.
  ifneq ($(wildcard $(mode_mk)),)
    # Message helper: empty if MODE=debug, otherwise " MODE=$(MODE)".
    mode_flag = $(if $(filter debug,$(MODE)),, MODE=$(MODE))

    # Detect changes to all dir_vars, except for cur_dir.
    test_dir_vars = $(filter-out cur_dir,$(dir_vars))
    changed_dirs = $(strip $(foreach dv,$(test_dir_vars),$(if $(filter $($(dv)),$(orig_$(dv))),,$(dv))))

    ifneq ($(changed_dirs),)
      ifeq ($(FORCE),1)
        $(info Proceeding with FORCE=1, despite changed dir values: $(changed_dirs))
        ifndef mixed_dirs_forced_from
          # Set flag to show a 'mixed directory' message from now until 'make clean' is run.
          $(file >>$(mode_mk), mixed_dirs_forced_from = $(cur_dir))
        endif
      else
        # Summarize dir values that differ.
        $(info Changed paths detected! ($(changed_dirs)))

        # Display a message for each changed dir value, i.e. $(orig_*_dir) != $(*_dir).
        changed_dir_msg = $(info $(1) was = $(orig_$(1)))$(info $(1) now = $($(1)))
        $(foreach dv,$(changed_dirs),$(call changed_dir_msg,$(dv)))

        # Suggest further actions and stop; include MODE=... if it's not "debug".
        ifneq ($(orig_cur_dir),$(cur_dir))
          $(info - Retry from $(orig_cur_dir), or)
        endif
        $(info - Retry after 'make$(mode_flag) clean', or)
        $(info - Retry after 'make$(mode_flag) cleandeps' with FORCE=1 (strange results may occur))
        $(error Changed dir values detected)
      endif
    endif

    # Warn if FORCE=1 was ever used for this mode directory.
    ifdef mixed_dirs_forced_from
      $(info Build forced with different paths; strange results may occur.)
      $(info Run 'make$(mode_flag) clean' to clear this warning.)
    endif
  endif
endif
