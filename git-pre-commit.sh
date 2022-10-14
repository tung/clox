#!/bin/sh
#
# To install: ln -s ../../git-pre-commit.sh .git/hooks/pre-commit

set -eu

STASH_NAME="pre-commit-$(date -Iseconds)"
git stash push --keep-index --include-untracked "${STASH_NAME}"

make checkformat

if [ "$(git stash list | head -n 1)" = "*${STASH_NAME}" ]; then
  git stash pop
fi
