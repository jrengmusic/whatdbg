#!/bin/bash
# Usage: bash release.sh v0.0.2 "feat: ARM64 build support"

TAG="${1:?Usage: release.sh <tag> <message>}"
MSG="${2:?Usage: release.sh <tag> <message>}"

# Delete existing tag if present
if git rev-parse "$TAG" >/dev/null 2>&1; then
    echo "Tag $TAG exists — removing local and remote"
    git tag -d "$TAG"
    git push origin ":refs/tags/$TAG" 2>/dev/null
fi

git add -A
git commit -m "$MSG"
git tag "$TAG"
git push origin main "$TAG"
