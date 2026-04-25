#!/usr/bin/env bash
set -e

# Usage: ./release.sh <tag>
# Expects dist/*.zip to exist (from build.sh runs on each platform).
# Creates a GitHub Release with all zips attached.

TAG="${1:?Usage: ./release.sh <tag>}"

# Verify dist/ has zips
if ! ls dist/*.zip 1>/dev/null 2>&1; then
    echo "No zips found in dist/. Run build.sh on each platform first."
    exit 1
fi

echo "Release: $TAG"
echo "Assets:"
ls -lh dist/*.zip

# Clean up any existing release/tag for this version
gh release delete "$TAG" --cleanup-tag -y 2>/dev/null || true
git tag -d "$TAG" 2>/dev/null || true
git push origin ":refs/tags/$TAG" 2>/dev/null || true

# Tag and push
git add -A
git diff --cached --quiet || git commit -m "$TAG"
git tag "$TAG"
git push origin main "$TAG"

# Create GitHub Release with all zips
gh release create "$TAG" dist/*.zip --title "$TAG" --generate-notes
echo "Done: https://github.com/jrengmusic/whatdbg/releases/tag/$TAG"
