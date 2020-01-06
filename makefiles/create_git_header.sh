#!/bin/sh

COMMIT="$(git rev-parse HEAD)"
BRANCH="$(git rev-parse --abbrev-ref HEAD)"
echo "#define GIT_COMMIT \"${COMMIT}\"" > $1
echo "#define GIT_BRANCH \"${BRANCH}\"" >> $1
