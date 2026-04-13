#!/bin/bash
# Usage: bash release.sh v0.0.1 "Optional commit message"
# CI workflow builds and creates GitHub Release on tag push

TAG="${1:?Usage: release.sh <tag> [message]}"
MSG="${2:-$TAG}"

# Delete existing release and tag if present
if git rev-parse "$TAG" >/dev/null 2>&1; then
    echo "Tag $TAG exists — removing release, tag (local + remote)"
    gh release delete "$TAG" --cleanup-tag -y 2>/dev/null
    git tag -d "$TAG" 2>/dev/null
    git push origin ":refs/tags/$TAG" 2>/dev/null
fi

git add -A
git commit -m "$MSG"
git tag "$TAG"
git push origin main "$TAG"
