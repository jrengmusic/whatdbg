#!/bin/bash
# Usage: bash release.sh v0.1.1 "feat: ARM64 build support"

TAG="${1:?Usage: release.sh <tag> <message>}"
MSG="${2:?Usage: release.sh <tag> <message>}"

git add -A
git commit -m "$MSG"
git tag "$TAG"
git push origin main "$TAG"
