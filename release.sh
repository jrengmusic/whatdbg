#!/bin/bash
# Usage: bash release.sh v0.0.1 "Optional commit message"
# CI workflow builds and creates GitHub Release on tag push

TAG="${1:?Usage: release.sh <tag> [message]}"
MSG="${2:-$TAG}"

# Always clean up — local, remote, and GitHub release
gh release delete "$TAG" --cleanup-tag -y 2>/dev/null
git tag -d "$TAG" 2>/dev/null
git push origin ":refs/tags/$TAG" 2>/dev/null

git add -A
git diff --cached --quiet || git commit -m "$MSG"
git tag "$TAG"
git push origin main "$TAG"
