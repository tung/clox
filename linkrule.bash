#!/usr/bin/env bash

# Makefile link rule helper script.
#
# Usage: ./linkrule.bash foo build/foo.d > build/foo.link.d
#
# Include build/foo.link.d in the Makefile along with a link rule, e.g.
#
# foo:
#     $(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
#
# Lists object files that should be linked into a binary as a Makefile rule.
# For each build/*.d file, include any build/*.o files mentioned and
# recursively visit other build/*.d files based on src/*.h file mentions.
# Requires basic build/*.d files to be present first.

set -Eeuo pipefail

# Check that the script arguments are in order before proceeding.
if [[ $# -lt 2 ]]; then exit 1; fi
if [[ -z ${1} ]]; then exit 1; fi
if [[ ! ${2} =~ ^build/.*\.d$ ]]; then exit 1; fi

deps=("${2}")
objs=()
d=0

# Loop through each *.d file.
while [[ ${d} -lt ${#deps[*]} ]]; do
  #echo DEPEND ${d} "${deps[d]}"

  # Check if the *.d file exists; silently skip it if it doesn't.
  if [[ -f ${deps[d]} ]]; then
    # Loop through each line in the *.d file.
    while read -r -a words; do
      #echo ..LINE "${words[@]}"

      # Loop through whitespace-separated words in the line.
      for word in "${words[@]}"; do
        #echo ..WORD "${word}"
        if [[ ${word} =~ ^(build/.*\.o)\:?$ ]]; then
          #echo NEWOBJ "${BASH_REMATCH[1]}"

          # Word matched "build/*.o"; add it to the object list
          # if it's not already there.
          newobj=${BASH_REMATCH[1]}
          if [[ ! ${objs[*]} =~ (^| )"${newobj}"($| ) ]]; then
            #echo ADDOBJ "${newobj}"
            objs+=("${newobj}")
          fi
        elif [[ ${word} =~ ^src/(.*)\.h$ ]]; then
          #echo NEWDEP "${BASH_REMATCH[1]}"

          # Word matched "src/*.h"; turn it into "build/*.d" and
          # add that to the dependencies to search if it's not
          # already there.
          newdep=build/${BASH_REMATCH[1]}.d
          if [[ ! ${deps[*]} =~ (^| )"${newdep}"($| ) ]]; then
            #echo ADDDEP "${newdep}"
            deps+=("${newdep}")
          fi
        fi
      done
    done <"${deps[d]}"
  fi

  # Advance to next *.d file.
  ((++d))
done

# Emit the Makefile rule, e.g. foo: bar.o baz.o
printf "%s: %s\n" "${1}" "${objs[*]}"
