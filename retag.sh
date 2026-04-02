#!/usr/bin/env bash
# retag.sh — commit, delete old tag, re-tag, push everything
# Usage: ./retag.sh v0.0.1 "fix: cmake minimum version"

set -e

TAG="${1:?Usage: retag.sh <tag> <commit message>}"
MSG="${2:?Usage: retag.sh <tag> <commit message>}"

git add -A
git commit -m "$MSG"
git tag -d "$TAG" 2>/dev/null || true
git push origin ":refs/tags/$TAG" 2>/dev/null || true
git tag "$TAG"
git push
git push origin "$TAG"

echo "Done. Tag $TAG pushed."
